#pragma once

#include "calibrate.hpp"
#include "capture.hpp"
#include "config.hpp"
#include "input.hpp"
#include "recognizer.hpp"
#include "timer.hpp"
#include "ui.hpp"

#include <optional>

namespace valinvite {

class App final {
public:
    explicit App(HINSTANCE instance);
    [[nodiscard]] int run();
    void onCandidate(const Candidate& candidate);

private:
    void start();
    void stop();
    void handleHotkey(WPARAM hotkeyId);
    void persistCalibration();

    HINSTANCE instance_{};
    Config config_{};
    ConfigStore configStore_{L"config.json"};
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
};

} // namespace valinvite
