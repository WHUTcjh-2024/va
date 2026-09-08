#include "capture.hpp"

namespace valinvite {

bool Capture::start(HWND sourceWindow, Rect roi, std::wstring& error) {
    if (!IsWindow(sourceWindow)) {
        error = L"直播窗口无效";
        return false;
    }
    if (!roi.valid()) {
        error = L"ROI 尚未校准";
        return false;
    }
    // P0.3 replaces this state transition with WGC/D3D11 event-driven capture.
    sourceWindow_ = sourceWindow;
    roi_ = roi;
    running_ = true;
    return true;
}

void Capture::stop() noexcept {
    running_ = false;
    sourceWindow_ = nullptr;
    roi_ = {};
}

bool Capture::running() const noexcept { return running_; }

} // namespace valinvite
