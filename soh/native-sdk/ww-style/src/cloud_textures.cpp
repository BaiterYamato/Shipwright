// Port do gen-ww-cloud-textures.py do fork: mesma conta, mesma ordem de chamadas ao RNG e o round() do Python, para
// sair byte a byte igual aos PNGs que o fork empacotava. As nuvens soltas são a união de elipses macias (um núcleo
// largo e chato e uma coroa de bolhas) com a base em ardósia; as tiras do horizonte são fileiras de bolhas
// agrupadas, periódicas em X.
#include "cloud_textures.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace WWStyle {
namespace {

constexpr double kPi = 3.141592653589793; // math.pi

// O LCG do gerador em precisão dupla, como o Python.
struct GenRng {
    uint32_t s;
    double Next01() {
        s = (s * 1664525u) + 1013904223u;
        return (s >> 8) / 16777216.0;
    }
    double Range(double a, double b) {
        return a + ((b - a) * Next01());
    }
};

double Smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

// round() do Python: metade vai para o par.
uint8_t RoundU8(double v) {
    return static_cast<uint8_t>(std::nearbyint(v));
}

// Branco para ardósia; t = quanto de sombra, 0..1.
void ShadePx(uint8_t* px, double alpha, double t) {
    px[0] = RoundU8(246.0 - (t * (246.0 - 148.0)));
    px[1] = RoundU8(248.0 - (t * (248.0 - 176.0)));
    px[2] = RoundU8(250.0 - (t * (250.0 - 205.0)));
    px[3] = RoundU8(alpha * 255.0);
}

struct Blob {
    double cx;
    double cy;
    double rx;
    double ry;
};

double BandKernel(double d2) {
    if (d2 >= 1.0) return 0.0;
    const double u = 1.0 - d2;
    return u * u;
}

} // namespace

void GenerateCloudSprite(uint32_t seed, uint8_t* out) {
    constexpr int size = kCloudSpriteSize;
    GenRng rng{seed};
    Blob blobs[5] = {{32.0, 38.0, 17.0, 9.0}}; // núcleo largo e chato
    for (int k = 0; k < 4; ++k) {
        // Coroa de bolhas por cima, sobre o núcleo. A ordem das chamadas ao RNG é a do Python: u, raio, altura.
        const double u = ((k + 0.5) / 4.0) + rng.Range(-0.05, 0.05);
        const double r = rng.Range(7.0, 11.0);
        const double cy = 36.0 - (std::sin(u * kPi) * rng.Range(3.0, 7.0));
        blobs[k + 1] = {14.0 + (36.0 * u), cy, r, r * 0.85};
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double alpha = 0.0;
            double interior = 0.0;
            for (const Blob& b : blobs) {
                const double dn = std::hypot((x + 0.5 - b.cx) / b.rx, (y + 0.5 - b.cy) / b.ry);
                alpha = std::max(alpha, Smoothstep(1.0, 0.80, dn));
                if (dn < 1.0) interior += 1.0 - dn;
            }
            double t = Smoothstep(0.48, 0.80, static_cast<double>(y) / size) * 0.62;
            t *= 1.0 - (0.35 * Smoothstep(1.0, 2.2, interior));
            ShadePx(out + (((y * size) + x) * 4), alpha, t);
        }
    }
}

void GenerateCloudBand(uint32_t seed, int clusters, int perCluster, double tallness, bool floorFill, uint8_t* out) {
    constexpr int width = kCloudBandWidth;
    constexpr int height = kCloudBandHeight;
    GenRng rng{seed};
    std::vector<Blob> blobs;
    blobs.reserve(static_cast<size_t>(clusters) * perCluster);
    for (int c = 0; c < clusters; ++c) {
        const double base = ((c + rng.Range(0.1, 0.9)) * width) / clusters;
        for (int i = 0; i < perCluster; ++i) {
            const double cx = base + rng.Range(-26.0, 26.0);
            const double rx = rng.Range(13.0, 26.0);
            const double ry = rng.Range(8.0, 13.0) * tallness;
            const double cy = height - rng.Range(0.0, 8.0) - (ry * 0.30);
            double wrapped = std::fmod(cx, static_cast<double>(width)); // % do Python: sinal do divisor
            if (wrapped < 0.0) wrapped += width;
            blobs.push_back({wrapped, cy, rx, ry});
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double f = 0.0;
            for (const Blob& b : blobs) {
                double dx = x + 0.5 - b.cx;
                dx -= width * std::nearbyint(dx / width); // volta periódica em X
                const double nx = dx / b.rx;
                const double ny = (y + 0.5 - b.cy) / b.ry;
                f += BandKernel((nx * nx) + (ny * ny));
            }
            if (floorFill) f += 1.5 * Smoothstep(height - 12.0, height + 3.0, y + 0.5);
            const double alpha = Smoothstep(0.40, 0.75, f);
            double t = Smoothstep(0.50, 0.96, static_cast<double>(y) / height) * 0.55;
            t *= 1.0 - (0.40 * Smoothstep(1.5, 3.0, f));
            ShadePx(out + (((y * width) + x) * 4), alpha, t);
        }
    }
}

} // namespace WWStyle
