#include "calibrate.hpp"

namespace valinvite {

void Calibrator::recordInputPoint(Config& config) const noexcept {
    POINT point{};
    if (GetCursorPos(&point)) config.inputPoint = {point.x, point.y};
}

void Calibrator::recordJoinPoint(Config& config) const noexcept {
    POINT point{};
    if (GetCursorPos(&point)) config.joinPoint = {point.x, point.y};
}

} // namespace valinvite
