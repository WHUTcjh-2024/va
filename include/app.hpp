#pragma once

#include "calibrate.hpp"
#include "capture.hpp"
#include "config.hpp"
#include "input.hpp"
#include "recognizer.hpp"
#include "timer.hpp"
#include "ui.hpp"

#include <atomic>
#include <filesystem>
#include <optional>

namespace valinvite {

class App final {
public:
    explicit App(HINSTANCE instance);
    ~App();
    [[nodiscard]] int run();
    void onCandidate(const Candidate& candidate);

private:
    void start();
    void stop();
    void handleHotkey(WPARAM hotkeyId);
    void persistCalibration();
    void reportWarning(const std::wstring& message);
    void applyPerformancePolicy();
    void onFrame(const BgraRoiFrame& frame);
    void processRecognizedCandidate(Candidate candidate, double recognitionMs);

    HINSTANCE instance_{};
    std::filesystem::path resourceDirectory_;
    Config config_{};
    ConfigStore configStore_;
    Calibrator calibrator_{};
    Capture capture_{};
    Recognizer recognizer_{};
    InputDispatcher input_{};
    HighResolutionTimer timer_{};
    Ui ui_{};
    RunState state_{RunState::Setup};
    TimingSnapshot timing_{};
    std::optional<std::string> pendingCandidate_;
    std::optional<std::string> lastSubmittedCode_;
    std::wstring lastError_;
    std::atomic_bool acceptingFrames_{false};
};

} // namespace valinvite
