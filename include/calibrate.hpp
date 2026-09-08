#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace valinvite {

struct CaptureWindow final {
    HWND handle{};
    std::wstring title;
    Rect clientAreaOnScreen;
};

class Calibrator final {
public:
    [[nodiscard]] std::vector<CaptureWindow> enumerateCaptureWindows() const;
    [[nodiscard]] bool selectCaptureWindow(Config& config, HWND window, std::wstring& error) const;
    [[nodiscard]] bool selectCaptureWindowAtCursor(Config& config, std::wstring& error) const;
    // A modal transparent overlay. ROI is saved relative to the Client Area.
    [[nodiscard]] bool selectRoi(Config& config, HWND window, std::wstring& error) const;
    [[nodiscard]] bool resolveRoi(const Config& config, HWND window, Rect& roi, std::wstring& error) const;
    void recordInputPoint(Config& config) const noexcept;
    void recordJoinPoint(Config& config) const noexcept;
};

} // namespace valinvite
