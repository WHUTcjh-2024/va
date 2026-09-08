#pragma once

#include "types.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace valinvite {

class HighResolutionTimer final {
public:
    HighResolutionTimer();
    [[nodiscard]] double elapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const noexcept;
    [[nodiscard]] LARGE_INTEGER now() const noexcept;
    [[nodiscard]] MemorySnapshot memory() const noexcept;

    // Call from the hot path only with an already measured duration; no I/O or
    // allocation is performed after reserveSamples().
    void reserveSamples(std::size_t count);
    void record(std::string_view metric, double milliseconds);
    [[nodiscard]] LatencyPercentiles percentiles(std::string_view metric) const;
    void clear() noexcept;

private:
    struct Series final {
        std::string_view name;
        std::vector<double> values;
    };
    [[nodiscard]] Series* series(std::string_view metric) noexcept;
    [[nodiscard]] const Series* series(std::string_view metric) const noexcept;
    double ticksPerMillisecond_{};
    std::array<Series, 3> series_{};
};

} // namespace valinvite
