#include "water_ripple_field.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace water {
namespace {
Config Validate(Config c, Point focus) {
    const auto positive = [](float v) { return std::isfinite(v) && v > 0.0f; };
    const auto nonnegative = [](float v) { return std::isfinite(v) && v >= 0.0f; };
    const double lambda = double(c.waveSpeed) * c.stepSeconds / c.cellSize;
    const double dampingStep = (double(c.damping) + c.edgeDamping) * c.stepSeconds;
    if (c.side < 3 || c.side > 4096 || !positive(c.cellSize) || !positive(c.stepSeconds) ||
        !positive(c.waveSpeed) || !nonnegative(c.damping) || !nonnegative(c.edgeDamping) ||
        !positive(c.maxImpulseSpeed) || !std::isfinite(c.planeY) ||
        !std::isfinite(focus.x) || !std::isfinite(focus.z) ||
        !std::isfinite(lambda) || lambda <= 0.0 || lambda * lambda > 0.49 ||
        dampingStep > 1.0 || c.edgeCells > c.side / 2 ||
        double(c.side) * c.cellSize > std::numeric_limits<float>::max() / 16.0 ||
        double(c.maxImpulseSpeed) * c.stepSeconds > std::numeric_limits<float>::max() / 64.0) {
        throw std::invalid_argument("water::Field: configuracao invalida ou CFL excedida");
    }
    return c;
}

std::uint16_t Half(float value) noexcept {
    // Conversão independente de extensões/SIMD; binário IEEE 754 binary32 -> binary16.
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    value = std::clamp(value, -65504.0f, 65504.0f);
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = (bits >> 16) & 0x8000u;
    int exponent = int((bits >> 23) & 255u) - 127 + 15;
    auto mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;
        const auto shift = static_cast<unsigned>(14 - exponent);
        const auto rounded = (mantissa + ((1u << (shift - 1)) - 1u) + ((mantissa >> shift) & 1u)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    mantissa += 0xfffu + ((mantissa >> 13) & 1u);
    if (mantissa & 0x800000u) { mantissa = 0; ++exponent; }
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7bffu);
    return static_cast<std::uint16_t>(sign | (static_cast<unsigned>(exponent) << 10) | (mantissa >> 13));
}
} // namespace

Field::Field(Config config, Point focus)
    : config_(Validate(config, focus)), height_(config_.side * config_.side, 0.0f),
      previous_(height_.size(), 0.0f) {
    const double lambda = double(config_.waveSpeed) * config_.stepSeconds / config_.cellSize;
    lambdaSquared_ = static_cast<float>(lambda * lambda);
    Follow(focus.x, focus.z);
}

void Field::Reset() noexcept {
    std::fill(height_.begin(), height_.end(), 0.0f);
    std::fill(previous_.begin(), previous_.end(), 0.0f);
    steps_ = 0;
}

void Field::ShiftBuffer(std::vector<float>& buffer, int dx, int dz) noexcept {
    const int n = static_cast<int>(config_.side);
    // A origem fonte fica à frente da escrita na ordem escolhida. Não precisa scratch.
    for (int row = 0; row < n; ++row) {
        const int z = dz >= 0 ? row : n - 1 - row;
        for (int col = 0; col < n; ++col) {
            const int x = dx >= 0 ? col : n - 1 - col;
            const int sx = x + dx, sz = z + dz;
            buffer[Index(x, z)] = sx >= 0 && sx < n && sz >= 0 && sz < n ? buffer[Index(sx, sz)] : 0.0f;
        }
    }
}

bool Field::Follow(float focusX, float focusZ) noexcept {
    if (!std::isfinite(focusX) || !std::isfinite(focusZ)) return false;
    const double spacing = config_.cellSize;
    const double x = (std::floor(double(focusX) / spacing) - double(config_.side / 2)) * spacing;
    const double z = (std::floor(double(focusZ) / spacing) - double(config_.side / 2)) * spacing;
    const double dx = std::round((x - originX_) / spacing), dz = std::round((z - originZ_) / spacing);
    if (dx == 0.0 && dz == 0.0) return false;
    if (std::abs(dx) >= double(config_.side) || std::abs(dz) >= double(config_.side)) {
        // Teleporte não altera o contador temporal.
        std::fill(height_.begin(), height_.end(), 0.0f);
        std::fill(previous_.begin(), previous_.end(), 0.0f);
    } else {
        ShiftBuffer(height_, static_cast<int>(dx), static_cast<int>(dz));
        ShiftBuffer(previous_, static_cast<int>(dx), static_cast<int>(dz));
    }
    originX_ = x;
    originZ_ = z;
    return true;
}

float Field::WorldX(std::size_t x) const noexcept { return static_cast<float>(originX_ + double(x) * config_.cellSize); }
float Field::WorldZ(std::size_t z) const noexcept { return static_cast<float>(originZ_ + double(z) * config_.cellSize); }

float Field::HeightAt(int x, int z) const noexcept {
    const int n = static_cast<int>(config_.side);
    return x >= 0 && x < n && z >= 0 && z < n ? height_[Index(x, z)] : 0.0f;
}

Gradient Field::GradientAt(int x, int z) const noexcept {
    const float scale = 0.5f / config_.cellSize;
    return {(HeightAt(x + 1, z) - HeightAt(x - 1, z)) * scale,
            (HeightAt(x, z + 1) - HeightAt(x, z - 1)) * scale};
}

void Field::Step() noexcept {
    const int n = static_cast<int>(config_.side);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const auto i = Index(x, z);
            const float h = height_[i];
            const float lap = HeightAt(x - 1, z) + HeightAt(x + 1, z) +
                              HeightAt(x, z - 1) + HeightAt(x, z + 1) - 4.0f * h;
            const int edge = std::min({x, z, n - 1 - x, n - 1 - z});
            const float t = config_.edgeCells == 0 ? 0.0f :
                std::max(0.0f, 1.0f - float(edge) / float(config_.edgeCells));
            const float a = (config_.damping + config_.edgeDamping * t * t) * config_.stepSeconds;
            // O próximo tempo sobrescreve apenas o próprio h(t-dt).
            previous_[i] = (2.0f * h - (1.0f - a) * previous_[i] + lambdaSquared_ * lap) / (1.0f + a);
        }
    }
    height_.swap(previous_);
    ++steps_;
}

bool Field::Inject(float x, float z, float radius, float impulse) noexcept {
    return InjectMoving({x, z}, {x, z}, radius, impulse);
}

bool Field::InjectMoving(Point previous, Point current, float radius, float impulse) noexcept {
    if (!std::isfinite(previous.x) || !std::isfinite(previous.z) ||
        !std::isfinite(current.x) || !std::isfinite(current.z) ||
        !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(impulse) || impulse == 0.0f) return false;
    const double r = std::max(radius, config_.cellSize);
    const double ax = previous.x, az = previous.z;
    const double dx = double(current.x) - ax, dz = double(current.z) - az;
    const double length2 = dx * dx + dz * dz;
    const double r2 = r * r;
    const double kick = std::clamp(impulse, -config_.maxImpulseSpeed, config_.maxImpulseSpeed);
    bool affected = false;
    for (std::size_t z = 0; z < config_.side; ++z) {
        const double wz = originZ_ + double(z) * config_.cellSize;
        for (std::size_t x = 0; x < config_.side; ++x) {
            const double wx = originX_ + double(x) * config_.cellSize;
            const double t = length2 == 0.0 ? 0.0 : std::clamp(((wx - ax) * dx + (wz - az) * dz) / length2, 0.0, 1.0);
            const double ox = wx - ax - t * dx, oz = wz - az - t * dz;
            const double distance2 = ox * ox + oz * oz;
            if (distance2 >= r2) continue;
            const double w = 1.0 - distance2 / r2;
            const auto i = Index(x, z);
            const double velocity = (double(height_[i]) - previous_[i]) / config_.stepSeconds;
            const double v = std::clamp(velocity + kick * w * w,
                                       -double(config_.maxImpulseSpeed), double(config_.maxImpulseSpeed));
            previous_[i] = static_cast<float>(double(height_[i]) - v * config_.stepSeconds);
            affected = true;
        }
    }
    return affected;
}

bool Field::GridPosition(float x, float z, double& gx, double& gz) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(z)) return false;
    gx = (double(x) - originX_) / config_.cellSize;
    gz = (double(z) - originZ_) / config_.cellSize;
    return gx >= 0.0 && gz >= 0.0 && gx <= double(config_.side - 1) && gz <= double(config_.side - 1);
}

float Field::SampleHeight(float x, float z) const noexcept {
    double gx, gz;
    if (!GridPosition(x, z, gx, gz)) return 0.0f;
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const int jx = std::min(ix + 1, static_cast<int>(config_.side - 1));
    const int jz = std::min(iz + 1, static_cast<int>(config_.side - 1));
    const float tx = static_cast<float>(gx - ix), tz = static_cast<float>(gz - iz);
    return std::lerp(std::lerp(HeightAt(ix, iz), HeightAt(jx, iz), tx),
                     std::lerp(HeightAt(ix, jz), HeightAt(jx, jz), tx), tz);
}

Gradient Field::SampleGradient(float x, float z) const noexcept {
    double gx, gz;
    if (!GridPosition(x, z, gx, gz)) return {};
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const int jx = std::min(ix + 1, static_cast<int>(config_.side - 1));
    const int jz = std::min(iz + 1, static_cast<int>(config_.side - 1));
    const auto a = GradientAt(ix, iz), b = GradientAt(jx, iz), c = GradientAt(ix, jz), d = GradientAt(jx, jz);
    const float tx = static_cast<float>(gx - ix), tz = static_cast<float>(gz - iz);
    return {std::lerp(std::lerp(a.x, b.x, tx), std::lerp(c.x, d.x, tx), tz),
            std::lerp(std::lerp(a.z, b.z, tx), std::lerp(c.z, d.z, tx), tz)};
}

bool Field::PackTexture(std::span<float> rgb) const noexcept {
    if (rgb.size() < height_.size() * 3) return false;
    for (std::size_t z = 0; z < config_.side; ++z) {
        for (std::size_t x = 0; x < config_.side; ++x) {
            const auto i = Index(x, z);
            const auto g = GradientAt(static_cast<int>(x), static_cast<int>(z));
            rgb[3 * i] = height_[i]; rgb[3 * i + 1] = g.x; rgb[3 * i + 2] = g.z;
        }
    }
    return true;
}

bool Field::PackTextureRGBA16(std::span<std::uint16_t> rgba) const noexcept {
    if (rgba.size() < height_.size() * 4) return false;
    for (std::size_t z = 0; z < config_.side; ++z) {
        for (std::size_t x = 0; x < config_.side; ++x) {
            const auto i = Index(x, z);
            const auto g = GradientAt(static_cast<int>(x), static_cast<int>(z));
            rgba[4 * i] = Half(height_[i]); rgba[4 * i + 1] = Half(g.x);
            rgba[4 * i + 2] = Half(g.z); rgba[4 * i + 3] = 0;
        }
    }
    return true;
}

std::array<float, 4> Field::WorldToUV() const noexcept {
    const double scale = 1.0 / (double(config_.side) * config_.cellSize);
    const double halfTexel = 0.5 / double(config_.side);
    return {static_cast<float>(scale), static_cast<float>(scale),
            static_cast<float>(halfTexel - originX_ * scale), static_cast<float>(halfTexel - originZ_ * scale)};
}

double Field::Energy() const noexcept {
    // E(n-1/2) = 1/2 sum(v²) + c²/(2 dx²) sum_edges(Dh(n) Dh(n-1)).
    // Inclui ligações da borda com o exterior zero. Positiva para CFL < 1/2.
    // E(n+1/2)-E(n-1/2) = -2 dt sum(gamma * v_central²), salvo arredondamento.
    double energy = 0.0;
    const int n = static_cast<int>(config_.side);
    const double c2dx2 = double(config_.waveSpeed) * config_.waveSpeed /
                         (double(config_.cellSize) * config_.cellSize);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const auto i = Index(x, z);
            const double h = height_[i], p = previous_[i];
            const double v = (h - p) / config_.stepSeconds;
            energy += 0.5 * v * v;
            const double hx = x + 1 < n ? height_[Index(x + 1, z)] : 0.0;
            const double px = x + 1 < n ? previous_[Index(x + 1, z)] : 0.0;
            const double hz = z + 1 < n ? height_[Index(x, z + 1)] : 0.0;
            const double pz = z + 1 < n ? previous_[Index(x, z + 1)] : 0.0;
            energy += 0.5 * c2dx2 * ((hx - h) * (px - p) + (hz - h) * (pz - p));
            if (x == 0) energy += 0.5 * c2dx2 * h * p;
            if (z == 0) energy += 0.5 * c2dx2 * h * p;
        }
    }
    return energy;
}
} // namespace water
