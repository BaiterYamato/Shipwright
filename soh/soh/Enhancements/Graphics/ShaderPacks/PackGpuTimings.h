#pragma once

// Estatistica independente de D3D. P95 por nearest rank: ceil(0.95 * n).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ShaderPacks::PackGpu {
struct Summary {
    std::size_t count = 0;
    double median = 0, p95 = 0, maximum = 0;
};

template <std::size_t Capacity = 300> class Window {
    static_assert(Capacity > 0);
    std::array<double, Capacity> samples{};
    std::size_t count = 0;

  public:
    void Reset() { count = 0; }
    std::size_t Count() const { return count; }
    bool Full() const { return count == Capacity; }
    // Uma janela cheia so aceita nova amostra depois de Reset.
    bool AddMilliseconds(double value) {
        if (Full() || !std::isfinite(value) || value < 0)
            return false;
        samples[count++] = value;
        return true;
    }
    bool AddTicks(uint64_t begin, uint64_t end, uint64_t frequency, bool disjoint, bool ready = true) {
        if (!ready || disjoint || !frequency || end < begin)
            return false;
        return AddMilliseconds(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency));
    }
    Summary Read() const {
        if (!count)
            return {};
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.begin() + count);
        const auto middle = count / 2;
        const double median = count % 2 ? sorted[middle] : sorted[middle - 1] * 0.5 + sorted[middle] * 0.5;
        const auto rank95 = count - count / 20; // ceil(19 * count / 20), sem overflow.
        return { count, median, sorted[rank95 - 1], sorted[count - 1] };
    }
};
} // namespace ShaderPacks::PackGpu
