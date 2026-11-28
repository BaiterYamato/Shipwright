#pragma once

#ifdef _WIN32
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct PlayState;

namespace ShaderPacks {

struct SunShadowMesh {
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
};

struct SunShadowMap {
    static constexpr unsigned Size = 1024;
    // U, V, depth toward the light, then validity and texel width in world units.
    using Constants = std::array<std::array<float, 4>, 4>;
    Constants constants{};
    std::vector<float> depth;
};

std::shared_ptr<const SunShadowMesh> CaptureSunShadowMesh(const PlayState& play);
SunShadowMap BakeSunShadow(const SunShadowMesh& mesh);

} // namespace ShaderPacks
#endif
