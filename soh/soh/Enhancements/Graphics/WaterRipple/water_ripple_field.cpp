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
    const bool shallow = c.solver == Solver::ShallowWater;
    const double shallowCFL = (std::sqrt(2.0 * c.gravity * c.restDepth) + std::abs(double(c.backgroundCurrent.x)) +
                               std::abs(double(c.backgroundCurrent.z))) *
                              c.stepSeconds / c.cellSize;
    if (c.side < 3 || c.side > 4096 || !positive(c.cellSize) || !positive(c.stepSeconds) || !positive(c.waveSpeed) ||
        !nonnegative(c.damping) || !nonnegative(c.edgeDamping) || !positive(c.maxImpulseSpeed) ||
        !std::isfinite(c.planeY) || !std::isfinite(focus.x) || !std::isfinite(focus.z) ||
        (c.solver != Solver::DampedWave && !shallow) ||
        (!shallow && (!std::isfinite(lambda) || lambda <= 0.0 || lambda * lambda > 0.49)) ||
        (shallow && (!positive(c.gravity) || !positive(c.restDepth) || !std::isfinite(shallowCFL) || shallowCFL > 0.9 ||
                     double(c.gravity) * c.stepSeconds / c.cellSize > std::numeric_limits<float>::max() / 64.0 ||
                     double(c.restDepth) * c.stepSeconds / c.cellSize > std::numeric_limits<float>::max() / 64.0)) ||
        !std::isfinite(c.backgroundCurrent.x) || !std::isfinite(c.backgroundCurrent.z) || dampingStep > 1.0 ||
        c.edgeCells > c.side / 2 || double(c.side) * c.cellSize > std::numeric_limits<float>::max() / 16.0 ||
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
        if (exponent < -10)
            return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;
        const auto shift = static_cast<unsigned>(14 - exponent);
        const auto rounded = (mantissa + ((1u << (shift - 1)) - 1u) + ((mantissa >> shift) & 1u)) >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    mantissa += 0xfffu + ((mantissa >> 13) & 1u);
    if (mantissa & 0x800000u) {
        mantissa = 0;
        ++exponent;
    }
    if (exponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7bffu);
    return static_cast<std::uint16_t>(sign | (static_cast<unsigned>(exponent) << 10) | (mantissa >> 13));
}
} // namespace

std::uint16_t EncodeFlowXZ(Velocity velocity) noexcept {
    const auto quantize = [](float v) {
        if (!std::isfinite(v)) v = 0.0f;
        return int(std::round(std::clamp(v, -512.0f, 496.0f) / 16.0f)) + 32;
    };
    return Half(float(quantize(velocity.x) + 64 * quantize(velocity.z) - 2048));
}

Velocity DecodeFlowXZ(float alpha) noexcept {
    if (!std::isfinite(alpha)) return {};
    const int code = int(std::round(std::clamp(alpha, -2048.0f, 2047.0f))) + 2048;
    return { float((code % 64) - 32) * 16.0f, float((code / 64) - 32) * 16.0f };
}

bool Field::SetBackgroundCurrent(Point current) noexcept {
    const double cfl = (std::sqrt(2.0 * config_.gravity * config_.restDepth) +
                        std::abs(double(current.x)) + std::abs(double(current.z))) *
                       config_.stepSeconds / config_.cellSize;
    if (config_.solver != Solver::ShallowWater || !std::isfinite(current.x) ||
        !std::isfinite(current.z) || !std::isfinite(cfl) || cfl > 0.9) return false;
    config_.backgroundCurrent = current;
    return true;
}

Field::Field(Config config, Point focus)
    : config_(Validate(config, focus)), height_(config_.side * config_.side, 0.0f), previous_(height_.size(), 0.0f),
      velocityX_(config_.solver == Solver::ShallowWater ? config_.side * (config_.side + 1) : 0, 0.0f),
      velocityZ_(velocityX_.size(), 0.0f), scratchX_(velocityX_.size(), 0.0f), scratchZ_(velocityX_.size(), 0.0f) {
    if (config_.solver == Solver::DampedWave) {
        const double lambda = double(config_.waveSpeed) * config_.stepSeconds / config_.cellSize;
        lambdaSquared_ = static_cast<float>(lambda * lambda);
    }
    Follow(focus.x, focus.z);
}

void Field::ClearState() noexcept {
    std::fill(height_.begin(), height_.end(), 0.0f);
    std::fill(previous_.begin(), previous_.end(), 0.0f);
    std::fill(velocityX_.begin(), velocityX_.end(), 0.0f);
    std::fill(velocityZ_.begin(), velocityZ_.end(), 0.0f);
    std::fill(scratchX_.begin(), scratchX_.end(), 0.0f);
    std::fill(scratchZ_.begin(), scratchZ_.end(), 0.0f);
}

void Field::Reset() noexcept {
    ClearState();
    steps_ = 0;
}

void Field::ShiftBuffer(std::vector<float>& buffer, int dx, int dz, std::size_t width, std::size_t rows) noexcept {
    const int nx = static_cast<int>(width), nz = static_cast<int>(rows);
    // A origem fonte fica à frente da escrita na ordem escolhida. Não precisa scratch.
    for (int row = 0; row < nz; ++row) {
        const int z = dz >= 0 ? row : nz - 1 - row;
        for (int col = 0; col < nx; ++col) {
            const int x = dx >= 0 ? col : nx - 1 - col;
            const int sx = x + dx, sz = z + dz;
            buffer[z * width + x] = sx >= 0 && sx < nx && sz >= 0 && sz < nz ? buffer[sz * width + sx] : 0.0f;
        }
    }
}

bool Field::Follow(float focusX, float focusZ) noexcept {
    if (!std::isfinite(focusX) || !std::isfinite(focusZ))
        return false;
    const double spacing = config_.cellSize;
    const double x = (std::floor(double(focusX) / spacing) - double(config_.side / 2)) * spacing;
    const double z = (std::floor(double(focusZ) / spacing) - double(config_.side / 2)) * spacing;
    const double dx = std::round((x - originX_) / spacing), dz = std::round((z - originZ_) / spacing);
    if (dx == 0.0 && dz == 0.0)
        return false;
    if (std::abs(dx) >= double(config_.side) || std::abs(dz) >= double(config_.side)) {
        // Teleporte não altera o contador temporal.
        ClearState();
    } else {
        ShiftBuffer(height_, static_cast<int>(dx), static_cast<int>(dz), config_.side, config_.side);
        ShiftBuffer(previous_, static_cast<int>(dx), static_cast<int>(dz), config_.side, config_.side);
        if (config_.solver == Solver::ShallowWater) {
            ShiftBuffer(velocityX_, static_cast<int>(dx), static_cast<int>(dz), config_.side + 1, config_.side);
            ShiftBuffer(velocityZ_, static_cast<int>(dx), static_cast<int>(dz), config_.side, config_.side + 1);
            CloseFaces();
        }
    }
    originX_ = x;
    originZ_ = z;
    return true;
}

float Field::WorldX(std::size_t x) const noexcept {
    return static_cast<float>(originX_ + double(x) * config_.cellSize);
}
float Field::WorldZ(std::size_t z) const noexcept {
    return static_cast<float>(originZ_ + double(z) * config_.cellSize);
}

float Field::HeightAt(int x, int z) const noexcept {
    const int n = static_cast<int>(config_.side);
    return x >= 0 && x < n && z >= 0 && z < n ? height_[Index(x, z)] : 0.0f;
}

Gradient Field::GradientAt(int x, int z) const noexcept {
    const float scale = 0.5f / config_.cellSize;
    return { (HeightAt(x + 1, z) - HeightAt(x - 1, z)) * scale, (HeightAt(x, z + 1) - HeightAt(x, z - 1)) * scale };
}

void Field::Step() noexcept {
    if (config_.solver == Solver::ShallowWater) {
        StepShallow();
        ++steps_;
        return;
    }
    const int n = static_cast<int>(config_.side);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const auto i = Index(x, z);
            const float h = height_[i];
            const float lap =
                HeightAt(x - 1, z) + HeightAt(x + 1, z) + HeightAt(x, z - 1) + HeightAt(x, z + 1) - 4.0f * h;
            const int edge = std::min({ x, z, n - 1 - x, n - 1 - z });
            const float t =
                config_.edgeCells == 0 ? 0.0f : std::max(0.0f, 1.0f - float(edge) / float(config_.edgeCells));
            const float a = (config_.damping + config_.edgeDamping * t * t) * config_.stepSeconds;
            // O próximo tempo sobrescreve apenas o próprio h(t-dt).
            previous_[i] = (2.0f * h - (1.0f - a) * previous_[i] + lambdaSquared_ * lap) / (1.0f + a);
        }
    }
    height_.swap(previous_);
    ++steps_;
}

void Field::CloseFaces() noexcept {
    const auto n = config_.side;
    for (std::size_t i = 0; i < n; ++i) {
        velocityX_[i * (n + 1)] = velocityX_[i * (n + 1) + n] = 0.0f;
        velocityZ_[i] = velocityZ_[n * n + i] = 0.0f;
    }
}

float Field::DampingAt(int x, int z) const noexcept {
    const int n = static_cast<int>(config_.side);
    const int edge = std::min({ x, z, n - 1 - x, n - 1 - z });
    const float t = config_.edgeCells == 0 ? 0.0f : std::max(0.0f, 1.0f - float(edge) / float(config_.edgeCells));
    return config_.damping + config_.edgeDamping * t * t;
}

void Field::Advect(const std::vector<float>& source, std::vector<float>& target, std::size_t width,
                   std::size_t rows) noexcept {
    const double ax = double(config_.backgroundCurrent.x) * config_.stepSeconds / config_.cellSize;
    const double az = double(config_.backgroundCurrent.z) * config_.stepSeconds / config_.cellSize;
    // Um único fluxo por face, calculado identicamente pelas duas células.
    // Fluxo exterior zero: soma das alturas conserva volume mesmo com corrente.
    for (std::size_t z = 0; z < rows; ++z) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto i = z * width + x;
            const double q = source[i];
            const double left = x == 0 ? 0.0 : ax * (ax >= 0 ? source[i - 1] : q);
            const double right = x + 1 == width ? 0.0 : ax * (ax >= 0 ? q : source[i + 1]);
            const double down = z == 0 ? 0.0 : az * (az >= 0 ? source[i - width] : q);
            const double up = z + 1 == rows ? 0.0 : az * (az >= 0 ? q : source[i + width]);
            target[i] = static_cast<float>(q - (right - left) - (up - down));
        }
    }
}

void Field::StepShallow() noexcept {
    const auto n = config_.side;
    if (config_.backgroundCurrent.x != 0.0f || config_.backgroundCurrent.z != 0.0f) {
        Advect(height_, previous_, n, n);
        Advect(velocityX_, scratchX_, n + 1, n);
        Advect(velocityZ_, scratchZ_, n, n + 1);
        height_.swap(previous_);
        velocityX_.swap(scratchX_);
        velocityZ_.swap(scratchZ_);
        CloseFaces();
    }
    const double pressure = double(config_.gravity) * config_.stepSeconds / config_.cellSize;
    const double continuity = double(config_.restDepth) * config_.stepSeconds / config_.cellSize;
    // Velocidade em n-1/2 -> n+1/2, altura em n -> n+1 (forward/backward).
    // Arrasto trapezoidal: (1+a) u' = (1-a) u - g dt grad(h), a=gamma dt.
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t x = 1; x < n; ++x) {
            const auto i = z * (n + 1) + x;
            const double a =
                0.5 * config_.stepSeconds * (double(DampingAt(int(x - 1), int(z))) + DampingAt(int(x), int(z)));
            velocityX_[i] = static_cast<float>(
                ((1.0 - a) * velocityX_[i] - pressure * (double(height_[Index(x, z)]) - height_[Index(x - 1, z)])) /
                (1.0 + a));
        }
    }
    for (std::size_t z = 1; z < n; ++z) {
        for (std::size_t x = 0; x < n; ++x) {
            const auto i = z * n + x;
            const double a =
                0.5 * config_.stepSeconds * (double(DampingAt(int(x), int(z - 1))) + DampingAt(int(x), int(z)));
            velocityZ_[i] = static_cast<float>(
                ((1.0 - a) * velocityZ_[i] - pressure * (double(height_[Index(x, z)]) - height_[Index(x, z - 1)])) /
                (1.0 + a));
        }
    }
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t x = 0; x < n; ++x) {
            const double divergence = double(velocityX_[z * (n + 1) + x + 1]) - velocityX_[z * (n + 1) + x] +
                                      double(velocityZ_[(z + 1) * n + x]) - velocityZ_[z * n + x];
            height_[Index(x, z)] = static_cast<float>(double(height_[Index(x, z)]) - continuity * divergence);
        }
    }
}

bool Field::InjectShallow(Point previous, Point current, float radius, float impulse) noexcept {
    const auto n = config_.side;
    const double r = std::max(radius, config_.cellSize), r2 = r * r;
    const double dx = double(current.x) - previous.x, dz = double(current.z) - previous.z;
    const double length2 = dx * dx + dz * dz;
    const double kick = std::clamp(impulse, -config_.maxImpulseSpeed, config_.maxImpulseSpeed);
    const double limit = config_.maxImpulseSpeed;
    bool affected = false;
    const auto add = [&](float& face, double wx, double wz, bool xComponent) {
        const double t =
            length2 == 0.0 ? 0.0 : std::clamp(((wx - previous.x) * dx + (wz - previous.z) * dz) / length2, 0.0, 1.0);
        const double ox = wx - previous.x - t * dx, oz = wz - previous.z - t * dz;
        const double distance2 = ox * ox + oz * oz;
        if (distance2 >= r2)
            return;
        // u += kick*r²/(8H) grad(K). No centro de um disco resolvido,
        // -H div(u) ~= kick; a cápsula reta tem ganho transversal diferente.
        const double delta = -kick * (1.0 - distance2 / r2) * (xComponent ? ox : oz) / (2.0 * config_.restDepth);
        face = static_cast<float>(std::clamp(double(face) + delta, -limit, limit));
        affected = true;
    };
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t x = 1; x < n; ++x) {
            add(velocityX_[z * (n + 1) + x], originX_ + (double(x) - 0.5) * config_.cellSize,
                originZ_ + double(z) * config_.cellSize, true);
        }
    }
    for (std::size_t z = 1; z < n; ++z) {
        for (std::size_t x = 0; x < n; ++x) {
            add(velocityZ_[z * n + x], originX_ + double(x) * config_.cellSize,
                originZ_ + (double(z) - 0.5) * config_.cellSize, false);
        }
    }
    return affected;
}

bool Field::Inject(float x, float z, float radius, float impulse) noexcept {
    return InjectMoving({ x, z }, { x, z }, radius, impulse);
}

bool Field::InjectMoving(Point previous, Point current, float radius, float impulse) noexcept {
    if (!std::isfinite(previous.x) || !std::isfinite(previous.z) || !std::isfinite(current.x) ||
        !std::isfinite(current.z) || !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(impulse) ||
        impulse == 0.0f)
        return false;
    if (config_.solver == Solver::ShallowWater)
        return InjectShallow(previous, current, radius, impulse);
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
            if (distance2 >= r2)
                continue;
            const double w = 1.0 - distance2 / r2;
            const auto i = Index(x, z);
            const double velocity = (double(height_[i]) - previous_[i]) / config_.stepSeconds;
            const double v =
                std::clamp(velocity + kick * w * w, -double(config_.maxImpulseSpeed), double(config_.maxImpulseSpeed));
            previous_[i] = static_cast<float>(double(height_[i]) - v * config_.stepSeconds);
            affected = true;
        }
    }
    return affected;
}

bool Field::GridPosition(float x, float z, double& gx, double& gz) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(z))
        return false;
    gx = (double(x) - originX_) / config_.cellSize;
    gz = (double(z) - originZ_) / config_.cellSize;
    return gx >= 0.0 && gz >= 0.0 && gx <= double(config_.side - 1) && gz <= double(config_.side - 1);
}

float Field::SampleHeight(float x, float z) const noexcept {
    double gx, gz;
    if (!GridPosition(x, z, gx, gz))
        return 0.0f;
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const int jx = std::min(ix + 1, static_cast<int>(config_.side - 1));
    const int jz = std::min(iz + 1, static_cast<int>(config_.side - 1));
    const float tx = static_cast<float>(gx - ix), tz = static_cast<float>(gz - iz);
    return std::lerp(std::lerp(HeightAt(ix, iz), HeightAt(jx, iz), tx),
                     std::lerp(HeightAt(ix, jz), HeightAt(jx, jz), tx), tz);
}

Gradient Field::SampleGradient(float x, float z) const noexcept {
    double gx, gz;
    if (!GridPosition(x, z, gx, gz))
        return {};
    const int ix = static_cast<int>(gx), iz = static_cast<int>(gz);
    const int jx = std::min(ix + 1, static_cast<int>(config_.side - 1));
    const int jz = std::min(iz + 1, static_cast<int>(config_.side - 1));
    const auto a = GradientAt(ix, iz), b = GradientAt(jx, iz), c = GradientAt(ix, jz), d = GradientAt(jx, jz);
    const float tx = static_cast<float>(gx - ix), tz = static_cast<float>(gz - iz);
    return { std::lerp(std::lerp(a.x, b.x, tx), std::lerp(c.x, d.x, tx), tz),
             std::lerp(std::lerp(a.z, b.z, tx), std::lerp(c.z, d.z, tx), tz) };
}

Velocity Field::VelocityAt(std::size_t x, std::size_t z) const noexcept {
    if (config_.solver != Solver::ShallowWater)
        return {};
    const auto n = config_.side;
    return { config_.backgroundCurrent.x + 0.5f * (velocityX_[z * (n + 1) + x] + velocityX_[z * (n + 1) + x + 1]),
             config_.backgroundCurrent.z + 0.5f * (velocityZ_[z * n + x] + velocityZ_[(z + 1) * n + x]) };
}

Velocity Field::SampleVelocity(float x, float z) const noexcept {
    double gx, gz;
    if (!GridPosition(x, z, gx, gz))
        return {};
    const auto ix = static_cast<std::size_t>(gx), iz = static_cast<std::size_t>(gz);
    const auto jx = std::min(ix + 1, config_.side - 1), jz = std::min(iz + 1, config_.side - 1);
    const auto a = VelocityAt(ix, iz), b = VelocityAt(jx, iz), c = VelocityAt(ix, jz), d = VelocityAt(jx, jz);
    const float tx = static_cast<float>(gx - double(ix)), tz = static_cast<float>(gz - double(iz));
    return { std::lerp(std::lerp(a.x, b.x, tx), std::lerp(c.x, d.x, tx), tz),
             std::lerp(std::lerp(a.z, b.z, tx), std::lerp(c.z, d.z, tx), tz) };
}

bool Field::PackTexture(std::span<float> rgb) const noexcept {
    if (rgb.size() < height_.size() * 3)
        return false;
    for (std::size_t z = 0; z < config_.side; ++z) {
        for (std::size_t x = 0; x < config_.side; ++x) {
            const auto i = Index(x, z);
            const auto g = GradientAt(static_cast<int>(x), static_cast<int>(z));
            rgb[3 * i] = height_[i];
            rgb[3 * i + 1] = g.x;
            rgb[3 * i + 2] = g.z;
        }
    }
    return true;
}

bool Field::PackTextureRGBA16(std::span<std::uint16_t> rgba) const noexcept {
    return PackRGBA16(rgba, false, FlowComponent::X);
}

bool Field::PackTextureRGBA16Flow(std::span<std::uint16_t> rgba, FlowComponent component) const noexcept {
    if (component != FlowComponent::X && component != FlowComponent::Z)
        return false;
    return PackRGBA16(rgba, true, component);
}

bool Field::PackTextureRGBA16FlowXZ(std::span<std::uint16_t> rgba) const noexcept {
    return PackRGBA16(rgba, true, FlowComponent::X, true);
}

bool Field::PackRGBA16(std::span<std::uint16_t> rgba, bool flow, FlowComponent component, bool flowXZ) const noexcept {
    if (rgba.size() < height_.size() * 4)
        return false;
    for (std::size_t z = 0; z < config_.side; ++z) {
        for (std::size_t x = 0; x < config_.side; ++x) {
            const auto i = Index(x, z);
            const auto g = GradientAt(static_cast<int>(x), static_cast<int>(z));
            rgba[4 * i] = Half(height_[i]);
            rgba[4 * i + 1] = Half(g.x);
            rgba[4 * i + 2] = Half(g.z);
            const auto velocity = flow ? VelocityAt(x, z) : Velocity{};
            rgba[4 * i + 3] = flowXZ && config_.solver == Solver::ShallowWater ? EncodeFlowXZ(velocity) :
                              flow ? Half(component == FlowComponent::X ? velocity.x : velocity.z) : 0;
        }
    }
    return true;
}

std::array<float, 4> Field::WorldToUV() const noexcept {
    const double scale = 1.0 / (double(config_.side) * config_.cellSize);
    const double halfTexel = 0.5 / double(config_.side);
    return { static_cast<float>(scale), static_cast<float>(scale), static_cast<float>(halfTexel - originX_ * scale),
             static_cast<float>(halfTexel - originZ_ * scale) };
}

double Field::Energy() const noexcept {
    if (config_.solver == Solver::ShallowWater) {
        // Energia modificada do esquema forward/backward (tempo escalonado).
        // E = 1/2 sum(g*h² + H*|u|² - g*H*dt*u.grad(h)).
        // As faces exteriores são zero. Positiva dentro do CFL validado.
        const auto n = config_.side;
        const double g = config_.gravity, depth = config_.restDepth;
        const double cross = g * depth * config_.stepSeconds / config_.cellSize;
        double energy = 0.0;
        for (std::size_t z = 0; z < n; ++z) {
            for (std::size_t x = 0; x < n; ++x) {
                const double h = height_[Index(x, z)];
                energy += 0.5 * g * h * h;
                if (x > 0) {
                    const double u = velocityX_[z * (n + 1) + x];
                    energy += 0.5 * (depth * u * u - cross * u * (h - height_[Index(x - 1, z)]));
                }
                if (z > 0) {
                    const double v = velocityZ_[z * n + x];
                    energy += 0.5 * (depth * v * v - cross * v * (h - height_[Index(x, z - 1)]));
                }
            }
        }
        return energy;
    }
    // E(n-1/2) = 1/2 sum(v²) + c²/(2 dx²) sum_edges(Dh(n) Dh(n-1)).
    // Inclui ligações da borda com o exterior zero. Positiva para CFL < 1/2.
    // E(n+1/2)-E(n-1/2) = -2 dt sum(gamma * v_central²), salvo arredondamento.
    double energy = 0.0;
    const int n = static_cast<int>(config_.side);
    const double c2dx2 = double(config_.waveSpeed) * config_.waveSpeed / (double(config_.cellSize) * config_.cellSize);
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
            if (x == 0)
                energy += 0.5 * c2dx2 * h * p;
            if (z == 0)
                energy += 0.5 * c2dx2 * h * p;
        }
    }
    return energy;
}
} // namespace water
