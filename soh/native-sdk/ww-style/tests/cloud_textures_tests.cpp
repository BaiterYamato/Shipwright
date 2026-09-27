// Gerador das texturas de nuvem: byte a byte igual ao gen-ww-cloud-textures.py do fork. Os hashes vêm da saída do
// próprio script (FNV-1a 64 dos pixels RGBA por linha).
#include <chrono>
#include <cstdio>
#include <vector>

#include "cloud_textures.h"

namespace {
int failures = 0;

uint64_t Fnv1a(const std::vector<uint8_t>& bytes) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const uint8_t c : bytes) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

void Check(const char* name, const std::vector<uint8_t>& pixels, uint64_t expected) {
    const uint64_t got = Fnv1a(pixels);
    if (got != expected) {
        std::fprintf(stderr, "FALHOU: %s fnv=0x%016llX, esperado 0x%016llX\n", name,
                     static_cast<unsigned long long>(got), static_cast<unsigned long long>(expected));
        ++failures;
    }
}
} // namespace

int main() {
    using namespace WWStyle;
    const auto start = std::chrono::steady_clock::now();
    constexpr uint32_t kSpriteSeeds[3] = {0x1A2B3C4Du, 0x2B3C4D5Eu, 0x3C4D5E6Fu};
    constexpr uint64_t kSpriteHashes[3] = {0x3D5E27D02320A7A6ull, 0xCA2155F6F875A1CCull, 0x3179271D45B62D4Aull};
    const char* names[3] = {"cloudtx_01", "cloudtx_02", "cloudtx_03"};
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> sprite(kCloudSpriteSize * kCloudSpriteSize * 4);
        GenerateCloudSprite(kSpriteSeeds[i], sprite.data());
        Check(names[i], sprite, kSpriteHashes[i]);
    }
    std::vector<uint8_t> band(kCloudBandWidth * kCloudBandHeight * 4);
    GenerateCloudBand(0x5E6F7081u, 4, 4, 1.0, false, band.data());
    Check("cloud_mae", band, 0xBA7376DFBE81720Aull);
    GenerateCloudBand(0x4D5E6F70u, 5, 5, 1.5, true, band.data());
    Check("cloud_naka", band, 0x774F73DD77FAFAECull);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("cloud_textures_tests: %s (5 texturas em %.1f ms)\n", failures ? "falhou" : "ok", ms);
    return failures ? 1 : 0;
}
