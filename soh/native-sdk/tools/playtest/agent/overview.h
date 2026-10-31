#pragma once
#include "protocol.h"
#include <algorithm>
#include <stdexcept>

namespace Playtest {
// Value-only output. The caller supplies a live header exclusively inside oot.input.pad.
// Template also permits testing the real extraction against small SDK-shaped doubles.
template <class Header> Json OverviewCollision(const Header& h, const Json& a) {
    if ((h.numPolygons && !h.polyList) || (h.numVertices && !h.vtxList) || (h.numWaterBoxes && !h.waterBoxes))
        throw std::runtime_error("colisão estática incompleta");
    const uint64_t offset = a.value("offset", uint64_t(0)), limit = a.value("limit", 128u);
    const double ymin = a.at("y_min"), ymax = a.at("y_max");
    const auto bbox = a.value("bbox", Json::array({ h.minBounds.x, h.minBounds.z, h.maxBounds.x, h.maxBounds.z }));
    auto overlaps = [&](double x0, double z0, double x1, double z1) {
        return x1 >= bbox[0].template get<double>() && x0 <= bbox[2].template get<double>() &&
               z1 >= bbox[1].template get<double>() && z0 <= bbox[3].template get<double>();
    };
    Json j = { { "polygons", Json::array() },
               { "water_boxes", Json::array() },
               { "bbox", bbox },
               { "scene_bbox", { h.minBounds.x, h.minBounds.z, h.maxBounds.x, h.maxBounds.z } },
               { "counts", { { "polygons", h.numPolygons }, { "water_boxes", h.numWaterBoxes } } },
               { "invalid_polygons", 0 },
               { "next_offsets", Json::object() } };
    // Raw source cursors, NOT offsets into the filtered output. Scan work is bounded per callback.
    uint64_t i = std::min<uint64_t>(offset, h.numPolygons), scanned = 0;
    for (; i < h.numPolygons && scanned < 4096 && j["polygons"].size() < limit; ++i, ++scanned) {
        if (i % a.value("stride", 1u))
            continue;
        const auto& p = h.polyList[i];
        // COLPOLY_VTX_INDEX_MASK do host (z64bgcheck.h): 29 bits, não a máscara de 13 bits do vanilla.
        const uint32_t indices[] = { p.flags_vIA & 0x1FFFFFFFu, p.flags_vIB & 0x1FFFFFFFu, p.vIC & 0x1FFFFFFFu };
        if (indices[0] >= h.numVertices || indices[1] >= h.numVertices || indices[2] >= h.numVertices) {
            j["invalid_polygons"] = j["invalid_polygons"].template get<unsigned>() + 1;
            continue;
        }
        // Match StaticLookup_AddPoly in z_bgcheck.c, including strict thresholds.
        const char* kind =
            p.normal.y > int(0.5 * 32767) ? "floor" : (p.normal.y < int(-0.8 * 32767) ? "ceiling" : "wall");
        if (std::string_view(kind) == "ceiling" && !a.value("include_ceiling", false))
            continue;
        const auto& v0 = h.vtxList[indices[0]];
        const auto& v1 = h.vtxList[indices[1]];
        const auto& v2 = h.vtxList[indices[2]];
        if (std::max({ double(v0.y), double(v1.y), double(v2.y) }) < ymin ||
            std::min({ double(v0.y), double(v1.y), double(v2.y) }) > ymax ||
            !overlaps(std::min({ double(v0.x), double(v1.x), double(v2.x) }),
                      std::min({ double(v0.z), double(v1.z), double(v2.z) }),
                      std::max({ double(v0.x), double(v1.x), double(v2.x) }),
                      std::max({ double(v0.z), double(v1.z), double(v2.z) })))
            continue;
        j["polygons"].push_back(
            { { "index", i },
              { "kind", kind },
              { "surface_type", p.type },
              { "room", nullptr },
              { "normal", { p.normal.x / 32767.0, p.normal.y / 32767.0, p.normal.z / 32767.0 } },
              { "vertices", { { v0.x, v0.y, v0.z }, { v1.x, v1.y, v1.z }, { v2.x, v2.y, v2.z } } } });
    }
    j["scanned"] = scanned;
    j["next_offsets"]["offset"] = i < h.numPolygons ? Json(i) : Json(nullptr);
    i = std::min<uint64_t>(a.value("water_offset", uint64_t(0)), h.numWaterBoxes);
    scanned = 0;
    for (; i < h.numWaterBoxes && scanned < 4096 && j["water_boxes"].size() < limit; ++i, ++scanned) {
        const auto& w = h.waterBoxes[i];
        if (w.ySurface < ymin || w.ySurface > ymax || w.xLength < 0 || w.zLength < 0 ||
            !overlaps(w.xMin, w.zMin, double(w.xMin) + w.xLength, double(w.zMin) + w.zLength))
            continue;
        j["water_boxes"].push_back({ { "index", i },
                                     { "x_min", w.xMin },
                                     { "z_min", w.zMin },
                                     { "x_max", double(w.xMin) + w.xLength },
                                     { "z_max", double(w.zMin) + w.zLength },
                                     { "y", w.ySurface },
                                     { "room", w.room },
                                     { "not_swimmable", bool(w.notSwimmable) } });
    }
    j["next_offsets"]["water_offset"] = i < h.numWaterBoxes ? Json(i) : Json(nullptr);
    return j;
}
} // namespace Playtest
