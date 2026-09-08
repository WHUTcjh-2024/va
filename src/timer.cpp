#include "timer.hpp"

namespace valinvite {

HighResolutionTimer::HighResolutionTimer() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    ticksPerMillisecond_ = static_cast<double>(frequency.QuadPart) / 1000.0;
}

double HighResolutionTimer::elapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const noexcept {
    return static_cast<double>(end.QuadPart - start.QuadPart) / ticksPerMillisecond_;
}

LARGE_INTEGER HighResolutionTimer::now() const noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value;
}

} // namespace valinvite
