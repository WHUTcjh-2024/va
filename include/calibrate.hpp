#pragma once

#include "types.hpp"

namespace valinvite {

class Calibrator final {
public:
    void recordInputPoint(Config& config) const noexcept;
    void recordJoinPoint(Config& config) const noexcept;
};

} // namespace valinvite
