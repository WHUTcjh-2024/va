#pragma once

#include "types.hpp"

#include <string>

namespace valinvite {

class Ui final {
public:
    static constexpr UINT kCommandMessage = WM_APP + 1;
    static constexpr WPARAM kStartCommand = 1;
    static constexpr WPARAM kStopCommand = 2;

    [[nodiscard]] bool create(HINSTANCE instance, std::wstring& error);
    [[nodiscard]] HWND window() const noexcept;
    void update(RunState state, const Config& config, const TimingSnapshot& timing);
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND window_{};
    HWND status_{};
    HWND startButton_{};
    HWND stopButton_{};
};

} // namespace valinvite
