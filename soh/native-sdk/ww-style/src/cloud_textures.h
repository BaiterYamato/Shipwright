// Texturas das nuvens do céu: o gerador do gen-ww-cloud-textures.py do fork (arte original no estilo do Wind
// Waker, sem conteúdo da Nintendo), sem dependência do host para o teste comparar com a saída do Python.
#pragma once

#include <cstdint>

namespace WWStyle {

constexpr int kCloudSpriteSize = 64;
constexpr int kCloudBandWidth = 256;
constexpr int kCloudBandHeight = 64;

// RGBA32 por linha, em `out` de kCloudSpriteSize² × 4 bytes: nuvem solta (cloudtx_01..03).
void GenerateCloudSprite(uint32_t seed, uint8_t* out);
// RGBA32 por linha, em `out` de kCloudBandWidth × kCloudBandHeight × 4 bytes: tira do horizonte, periódica em X
// (cloud_mae e cloud_naka).
void GenerateCloudBand(uint32_t seed, int clusters, int perCluster, double tallness, bool floorFill, uint8_t* out);

} // namespace WWStyle
