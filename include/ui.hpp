#pragma once

#include "types.hpp"

#include <string>
#include <optional>

namespace valinvite {

class Ui final {
public:
    ~Ui();

    static constexpr UINT kCommandMessage = WM_APP + 1;
    static constexpr WPARAM kStartCommand = 1;
    static constexpr WPARAM kStopCommand = 2;
    static constexpr WPARAM kSelectWindowCommand = 3;
    static constexpr WPARAM kSelectRoiCommand = 4;

    [[nodiscard]] bool create(HINSTANCE instance, std::wstring& error);
    [[nodiscard]] HWND window() const noexcept;
    void update(RunState state, const Config& config, const TimingSnapshot& timing,
        const std::optional<Candidate>& candidate = std::nullopt, std::wstring_view error = {});
    void refreshWindows();
    [[nodiscard]] HWND selectedWindow() const noexcept;
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND window_{};
    HWND status_{};
    HWND startButton_{};
    HWND stopButton_{};
    HWND windowList_{};
    HWND roiButton_{};
    HFONT uiFont_{};
};

} // namespace valinvite
