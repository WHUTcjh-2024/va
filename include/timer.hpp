#pragma once

#include "types.hpp"

#include <windows.h>

namespace valinvite {

class HighResolutionTimer final {
public:
    HighResolutionTimer();
    [[nodiscard]] double elapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const noexcept;
    [[nodiscard]] LARGE_INTEGER now() const noexcept;

private:
    double ticksPerMillisecond_{};
};

} // namespace valinvite
