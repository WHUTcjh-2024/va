#pragma once

#include "types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace valinvite {

struct BgraRoiFrame final {
    // Owned by Capture and valid only for the duration of the callback.
    const std::uint8_t* pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t rowPitch{};
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point timestamp{};
};

struct CapturePreviewInfo final {
    Rect roiInClientArea{};
    std::uint32_t sourceWidth{};
    std::uint32_t sourceHeight{};
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point timestamp{};
};

class Capture final {
public:
    using FrameCallback = std::function<void(const BgraRoiFrame&)>;
    using PreviewCallback = std::function<void(const CapturePreviewInfo&)>;

    Capture();
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // roi is in physical pixels relative to sourceWindow's client area.
    [[nodiscard]] bool start(HWND sourceWindow, Rect roi, std::wstring& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    void setFrameCallback(FrameCallback callback);
    void setPreviewCallback(PreviewCallback callback);

private:
    struct Session;
    mutable std::mutex mutex_;
    std::shared_ptr<Session> session_;
    FrameCallback frameCallback_;
    PreviewCallback previewCallback_;
};

} // namespace valinvite
