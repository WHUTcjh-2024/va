#pragma once

#include "types.hpp"

#include <string_view>

namespace valinvite {

class InputDispatcher final {
public:
    [[nodiscard]] bool submit(std::string_view code, const Config& config, std::wstring& error) const;
};

} // namespace valinvite
