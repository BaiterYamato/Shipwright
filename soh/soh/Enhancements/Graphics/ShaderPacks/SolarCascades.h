#pragma once

// Matemática pura, sem headers do jogo ou do DirectX. Matrizes seguem o contrato
// row_major de LinkSpanCamera: mul(float4(world, 1), View).
#include <algorithm>
#include <array>
#include <cmath>

namespace ShaderPacks::SolarCascades {

constexpr unsigned MaxCascades = 3;
// Lado de cada cascata no atlas. O padrão é 1024; o jogador escolhe entre 512 e 4096 (GFX-UNIFY-001, 11/11).
constexpr unsigned TileSize = 1024;
constexpr unsigned MinTileSize = 512;
constexpr unsigned MaxTileSize = 4096; // 3 cascatas = 12288 de largura, dentro do limite de 16384 do DX11.

// Potência de dois mais próxima por baixo, dentro de [MinTileSize, MaxTileSize].
inline unsigned ClampTileSize(long long requested) {
    unsigned size = MinTileSize;
    while (size < MaxTileSize && static_cast<long long>(size) * 2 <= requested)
        size *= 2;
    return size;
}
constexpr unsigned ConstantVectors = 15;
constexpr double BlendFraction = 0.10;
using Vec3 = std::array<double, 3>;
using Vec4 = std::array<float, 4>;
using Rows = std::array<Vec4, 4>;
// [0..11] = U, V, luz, {válido, texel mundo, low, 1/depthSpan} por cascata;
// [12] = fins xyz + near; [13] = inícios do blend/fade xyz;
// [14] = {quantidade ativa, colunas do atlas, reservado, reservado}.
using Constants = std::array<Vec4, ConstantVectors>;
static_assert(sizeof(Constants) == 240);

struct Camera {
    float projection[4][4]{};
    float view[4][4]{};
    double nearDepth = 0;
    double farDepth = 0;
};

inline double Dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
inline Vec3 Scale(Vec3 a, double s) {
    for (auto& v : a)
        v *= s;
    return a;
}
inline Vec3 Add(Vec3 a, const Vec3& b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
inline bool Finite(const Vec3& v) {
    return std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
}

inline std::array<double, MaxCascades> SplitDepths(double nearDepth, double farDepth, unsigned count) {
    std::array<double, MaxCascades> ends{};
    if (count < 1 || count > MaxCascades || !std::isfinite(nearDepth) || !std::isfinite(farDepth) || nearDepth <= 0 ||
        farDepth <= nearDepth)
        return ends;
    constexpr double lambda = 0.65;
    for (unsigned i = 0; i < count; ++i) {
        const double t = double(i + 1) / count;
        ends[i] = lambda * nearDepth * std::pow(farDepth / nearDepth, t) +
                  (1 - lambda) * (nearDepth + (farDepth - nearDepth) * t);
    }
    ends[count - 1] = farDepth;
    return ends;
}

inline double BlendStart(double nearDepth, double farDepth) {
    return farDepth - (farDepth - nearDepth) * BlendFraction;
}

struct Frame {
    std::array<Vec3, 3> inverse{};
    Vec3 eye{};
    double stretch = 0;
};

inline Vec3 Transform(const Vec3& p, const Frame& f) {
    Vec3 result = f.eye;
    for (unsigned i = 0; i < 3; ++i)
        result = Add(result, Scale(f.inverse[i], p[i]));
    return result;
}

inline bool CameraFrame(const Camera& camera, Frame& frame) {
    for (const auto& row : camera.view)
        for (float x : row)
            if (!std::isfinite(x))
                return false;
    for (const auto& row : camera.projection)
        for (float x : row)
            if (!std::isfinite(x))
                return false;
    const auto& m = camera.view;
    const Vec3 a{ m[0][0], m[0][1], m[0][2] }, b{ m[1][0], m[1][1], m[1][2] }, c{ m[2][0], m[2][1], m[2][2] };
    const auto x = Cross(b, c), y = Cross(c, a), z = Cross(a, b);
    const double det = Dot(a, x);
    if (std::abs(det) < 1.0e-8 || std::abs(camera.projection[0][0]) < 1.0e-8 ||
        std::abs(camera.projection[1][1]) < 1.0e-8)
        return false;
    for (unsigned i = 0; i < 3; ++i)
        frame.inverse[i] = { x[i] / det, y[i] / det, z[i] / det };
    frame.eye = {};
    for (unsigned i = 0; i < 3; ++i)
        frame.eye = Add(frame.eye, Scale(frame.inverse[i], -m[3][i]));
    // Limite superior da norma espectral via Gram/Gershgorin. Vale também para
    // matrizes interpoladas não ortogonais; rotações rígidas têm stretch = 1.
    double bound = 0;
    for (unsigned i = 0; i < 3; ++i) {
        double row = 0;
        for (unsigned j = 0; j < 3; ++j)
            row += std::abs(Dot(frame.inverse[i], frame.inverse[j]));
        bound = std::max(bound, row);
    }
    // Não amplificar erro float de rotação; a margem de 3 texels abaixo cobre 1 ppm.
    frame.stretch = std::sqrt(std::max(1.0, bound));
    if (frame.stretch < 1.000001)
        frame.stretch = 1;
    return Finite(frame.eye) && std::isfinite(frame.stretch);
}

inline std::array<Vec3, 8> LocalCorners(const Camera& camera, double nearDepth, double farDepth) {
    std::array<Vec3, 8> corners{};
    const auto& p = camera.projection;
    unsigned i = 0;
    for (double d : { nearDepth, farDepth }) {
        const double w = -d * p[2][3] + p[3][3];
        for (double y : { -1.0, 1.0 })
            for (double x : { -1.0, 1.0 }) {
                corners[i++] = { (x * w + d * p[2][0] - p[3][0]) / p[0][0], (y * w + d * p[2][1] - p[3][1]) / p[1][1],
                                 -d };
            }
    }
    return corners;
}

struct Cascade {
    Rows sample{};
    Rows clip{};
    double radius = 0;
    double nearDepth = 0;
    double farDepth = 0;
};
struct Result {
    std::array<Cascade, MaxCascades> cascades{};
    std::array<double, MaxCascades> ends{};
    std::array<double, MaxCascades> blends{};
    unsigned count = 0;
};

inline Result Build(const Camera& camera, Vec3 light, unsigned count, unsigned tileSize = TileSize) {
    Result result;
    if (tileSize < MinTileSize || tileSize > MaxTileSize)
        return {};
    result.ends = SplitDepths(camera.nearDepth, camera.farDepth, count);
    Frame frame;
    const double length = std::sqrt(Dot(light, light));
    if (count < 1 || count > MaxCascades || result.ends[0] <= 0 || !Finite(light) || length < 1.0e-8 ||
        !CameraFrame(camera, frame))
        return {};
    light = Scale(light, 1 / length);
    Vec3 right = Cross(std::abs(light[2]) < 0.99 ? Vec3{ 0, 0, 1 } : Vec3{ 0, 1, 0 }, light);
    right = Scale(right, 1 / std::sqrt(Dot(right, right)));
    const Vec3 up = Cross(light, right);
    for (unsigned i = 0; i < count; ++i) {
        const double nominalNear = i ? result.ends[i - 1] : camera.nearDepth;
        result.blends[i] = BlendStart(nominalNear, result.ends[i]);
        auto& cascade = result.cascades[i];
        cascade.nearDepth = i ? result.blends[i - 1] : camera.nearDepth;
        cascade.farDepth = result.ends[i];
        const auto corners = LocalCorners(camera, cascade.nearDepth, cascade.farDepth);
        Vec3 localCenter{};
        for (const auto& v : corners)
            localCenter = Add(localCenter, Scale(v, 1.0 / corners.size()));
        double radius = 0;
        for (const auto& v : corners) {
            const Vec3 delta = Add(v, Scale(localCenter, -1));
            radius = std::max(radius, std::sqrt(Dot(delta, delta)));
        }
        // Esfera calculada no espaço da câmera: raio independe de yaw/pitch/roll.
        // Quantização + margem para snap de meio texel e kernel PCF de 4x4.
        radius = std::ceil(radius * frame.stretch * 16) / 16;
        cascade.radius = radius / (1 - 6.0 / tileSize);
        const double span = 2 * cascade.radius, texel = span / tileSize;
        const auto center = Transform(localCenter, frame);
        if (!Finite(center) || !std::isfinite(span) || span <= 0)
            return {};
        const auto plane = [&](const Vec3& axis) -> Vec4 {
            const double snapped = std::round(Dot(axis, center) / texel) * texel;
            return { float(axis[0] / span), float(axis[1] / span), float(axis[2] / span), float(0.5 - snapped / span) };
        };
        const auto u = plane(right), v = plane(up);
        const double depthSpan = span + 6400; // Extrusão para casters fora do frustum.
        const double low = std::round(Dot(light, center) / texel) * texel - depthSpan * 0.5;
        cascade.sample = { u,
                           v,
                           { float(light[0]), float(light[1]), float(light[2]), 0 },
                           { 1, float(texel), float(low), float(1 / depthSpan) } };
        cascade.clip = { { { 2 * u[0], 2 * u[1], 2 * u[2], 2 * u[3] - 1 },
                           { -2 * v[0], -2 * v[1], -2 * v[2], 1 - 2 * v[3] },
                           { float(light[0] / depthSpan), float(light[1] / depthSpan), float(light[2] / depthSpan),
                             float(-low / depthSpan) },
                           { 0, 0, 0, 1 } } };
    }
    result.count = count;
    return result;
}

} // namespace ShaderPacks::SolarCascades
