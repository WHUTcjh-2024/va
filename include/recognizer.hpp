#pragma once

#include "types.hpp"

#include <string_view>

namespace valinvite {

class Recognizer final {
public:
    [[nodiscard]] Candidate evaluate(std::string_view code) const;
    [[nodiscard]] bool shouldSubmit(const Candidate& candidate, const std::optional<std::string>& previous) const;
};

} // namespace valinvite
