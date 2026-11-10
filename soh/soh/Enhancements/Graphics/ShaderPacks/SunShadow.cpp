#include "SunShadow.h"

#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <limits>

extern "C" {
#include "z64.h"
}

namespace ShaderPacks {
namespace {

using Point = std::array<float, 3>;
constexpr size_t MaxVertices = 64 * 1024;
constexpr size_t MaxTriangles = 128 * 1024;
constexpr size_t MaxRasterPixels = 12 * SunShadowMap::Size * SunShadowMap::Size;
constexpr uint32_t IgnoresCameraAndEntities = 3u << COLPOLY_VIA_FLAGS_SHIFT;

float Dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Point Normalize(Point p) {
    const float length = std::sqrt(Dot(p, p));
    for (float& v : p) v /= length;
    return p;
}

Point Cross(const Point& a, const Point& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float Edge(const Point& a, const Point& b, float x, float y) {
    return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
}

int EdgeTexel(float coordinate) {
    return std::clamp(static_cast<int>(std::floor(coordinate)), 0, static_cast<int>(SunShadowMap::Size) - 1);
}

bool OutdoorShadowScene(int scene) {
    return scene == SCENE_KOKIRI_FOREST || scene == SCENE_LOST_WOODS || scene == SCENE_GRAVEYARD;
}

} // namespace

std::shared_ptr<const SunShadowMesh> CaptureSunShadowMesh(const PlayState& play) {
    // Called by the game thread. The snapshots own the copied mesh across room and scene transitions.
    static const CollisionHeader* previous = nullptr;
    static const Vec3i* previousVertices = nullptr;
    static const CollisionPoly* previousPolygons = nullptr;
    static int previousScene = -1;
    static std::shared_ptr<const SunShadowMesh> cached;
    const auto* header = OutdoorShadowScene(play.sceneNum) ? play.colCtx.colHeader : nullptr;
    const auto* vertices = header ? header->vtxList : nullptr;
    const auto* polygons = header ? header->polyList : nullptr;
    if (header == previous && vertices == previousVertices && polygons == previousPolygons &&
        play.sceneNum == previousScene) return cached;

    previous = header;
    previousVertices = vertices;
    previousPolygons = polygons;
    previousScene = play.sceneNum;
    cached.reset();
    if (!vertices || !polygons || header->numVertices < 3 || header->numVertices > MaxVertices ||
        header->numPolygons > MaxTriangles) return {};

    auto mesh = std::make_shared<SunShadowMesh>();
    mesh->vertices.reserve(header->numVertices);
    mesh->triangles.reserve(header->numPolygons);
    for (uint32_t i = 0; i < header->numVertices; ++i) {
        const auto& v = vertices[i];
        mesh->vertices.push_back({static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)});
    }
    for (uint32_t i = 0; i < header->numPolygons; ++i) {
        const auto& poly = polygons[i];
        if ((poly.flags_vIA & IgnoresCameraAndEntities) == IgnoresCameraAndEntities) continue;
        const uint32_t a = COLPOLY_VTX_INDEX(poly.flags_vIA);
        const uint32_t b = COLPOLY_VTX_INDEX(poly.flags_vIB);
        const uint32_t c = COLPOLY_VTX_INDEX(poly.vIC);
        if (std::max({a, b, c}) < header->numVertices && a != b && b != c && c != a) {
            mesh->triangles.push_back({a, b, c});
        }
    }
    if (!mesh->triangles.empty()) cached = std::move(mesh);
    return cached;
}

SunShadowMap BakeSunShadow(const SunShadowMesh& mesh) {
    // A fixed afternoon direction makes this experimental collision map stable as the camera moves.
    const Point light = Normalize({-0.52f, 0.73f, -0.44f});
    const Point right = Normalize(Cross({0.0f, 1.0f, 0.0f}, light));
    const Point up = Cross(light, right);
    std::vector<Point> points;
    points.reserve(mesh.vertices.size());
    Point lower{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0.0f};
    Point upper{-lower[0], -lower[1], 0.0f};
    for (const auto& v : mesh.vertices) {
        Point p{Dot(v, right), Dot(v, up), Dot(v, light)};
        points.push_back(p);
        for (size_t i = 0; i < 2; ++i) {
            lower[i] = std::min(lower[i], p[i]);
            upper[i] = std::max(upper[i], p[i]);
        }
    }
    constexpr float padding = 32.0f;
    const float width = upper[0] - lower[0] + 2 * padding, height = upper[1] - lower[1] + 2 * padding;
    const float originX = lower[0] - padding, originY = lower[1] - padding;
    const float texel = std::max(width, height) / SunShadowMap::Size;
    if (points.empty() || !std::isfinite(texel) || texel > 64.0f) return {};

    SunShadowMap map;
    map.constants = {{{right[0] / width, right[1] / width, right[2] / width, -originX / width},
                      {up[0] / height, up[1] / height, up[2] / height, -originY / height},
                      {light[0], light[1], light[2], 0.0f},
                      {1.0f, texel, 0.0f, 0.0f}}};
    for (auto& p : points) {
        p[0] = (p[0] - originX) * SunShadowMap::Size / width;
        p[1] = (p[1] - originY) * SunShadowMap::Size / height;
    }

    map.depth.resize(SunShadowMap::Size * SunShadowMap::Size, -1.0e20f);
    size_t rasterPixels = 0;
    for (const auto& triangle : mesh.triangles) {
        const Point& a = points[triangle[0]];
        const Point& b = points[triangle[1]];
        const Point& c = points[triangle[2]];
        const float area = Edge(a, b, c[0], c[1]);
        if (std::abs(area) < 0.001f) continue;
        const int x0 = EdgeTexel(std::min({a[0], b[0], c[0]})), x1 = EdgeTexel(std::max({a[0], b[0], c[0]}));
        const int y0 = EdgeTexel(std::min({a[1], b[1], c[1]})), y1 = EdgeTexel(std::max({a[1], b[1], c[1]}));
        rasterPixels += static_cast<size_t>(x1 - x0 + 1) * (y1 - y0 + 1);
        if (rasterPixels > MaxRasterPixels) return {};
        const float inverseArea = 1.0f / area;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float u = Edge(b, c, x + 0.5f, y + 0.5f) * inverseArea;
                const float v = Edge(c, a, x + 0.5f, y + 0.5f) * inverseArea;
                const float w = 1.0f - u - v;
                if (std::min({u, v, w}) < -0.00001f) continue;
                auto& depth = map.depth[static_cast<size_t>(y) * SunShadowMap::Size + x];
                depth = std::max(depth, u * a[2] + v * b[2] + w * c[2]);
            }
        }
    }
    return map;
}

} // namespace ShaderPacks
#endif
