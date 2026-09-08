#pragma once

#include "types.hpp"

namespace valinvite {

class Capture final {
public:
    [[nodiscard]] bool start(HWND sourceWindow, Rect roi, std::wstring& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    HWND sourceWindow_{};
    Rect roi_{};
    bool running_{};
};

} // namespace valinvite
