#include "app.hpp"

#include <string>

namespace valinvite {
namespace {
constexpr int kHotkeyInput = 1;
constexpr int kHotkeyJoin = 2;
constexpr int kHotkeyStart = 3;
constexpr int kHotkeyStop = 4;
}

App::App(HINSTANCE instance) : instance_{instance} {}

int App::run() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::wstring error;
    if (!configStore_.load(config_, error)) {
        MessageBoxW(nullptr, (L"将使用默认配置：\n" + error).c_str(), L"VAL Invite", MB_OK | MB_ICONINFORMATION);
    }
    if (!ui_.create(instance_, error)) {
        MessageBoxW(nullptr, error.c_str(), L"VAL Invite", MB_OK | MB_ICONERROR);
        return 1;
    }
    RegisterHotKey(ui_.window(), kHotkeyInput, 0, VK_F8);
    RegisterHotKey(ui_.window(), kHotkeyJoin, 0, VK_F9);
    RegisterHotKey(ui_.window(), kHotkeyStart, 0, VK_F10);
    RegisterHotKey(ui_.window(), kHotkeyStop, 0, VK_F11);
    ui_.update(state_, config_, timing_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY) handleHotkey(message.wParam);
        if (message.message == Ui::kCommandMessage) {
            if (message.wParam == Ui::kStartCommand) start();
            if (message.wParam == Ui::kStopCommand) stop();
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnregisterHotKey(ui_.window(), kHotkeyInput);
    UnregisterHotKey(ui_.window(), kHotkeyJoin);
    UnregisterHotKey(ui_.window(), kHotkeyStart);
    UnregisterHotKey(ui_.window(), kHotkeyStop);
    return static_cast<int>(message.wParam);
}

void App::start() {
    if (state_ != RunState::Setup && state_ != RunState::Stopped) return;
    state_ = RunState::Armed;
    pendingCandidate_.reset();
    ui_.update(state_, config_, timing_);
}

void App::stop() {
    capture_.stop();
    state_ = RunState::Stopped;
    pendingCandidate_.reset();
    ui_.update(state_, config_, timing_);
}

void App::handleHotkey(WPARAM hotkeyId) {
    if (hotkeyId == kHotkeyStart) {
        start();
        return;
    }
    if (hotkeyId == kHotkeyStop) {
        stop();
        return;
    }
    if (hotkeyId == kHotkeyInput) calibrator_.recordInputPoint(config_);
    if (hotkeyId == kHotkeyJoin) calibrator_.recordJoinPoint(config_);
    if (hotkeyId == kHotkeyInput || hotkeyId == kHotkeyJoin) persistCalibration();
    ui_.update(state_, config_, timing_);
}

void App::onCandidate(const Candidate& candidate) {
    if (state_ != RunState::Armed && state_ != RunState::Observing && state_ != RunState::Candidate) return;

    const LARGE_INTEGER decisionStart = timer_.now();
    state_ = RunState::Observing;
    if (!recognizer_.shouldSubmit(candidate, pendingCandidate_)) {
        pendingCandidate_ = candidate.structureValid ? std::optional<std::string>{candidate.code} : std::nullopt;
        if (!candidate.structureValid) lastSubmittedCode_.reset();
        timing_.decisionMs = timer_.elapsedMs(decisionStart, timer_.now());
        ui_.update(state_, config_, timing_);
        return;
    }
    if (lastSubmittedCode_ && *lastSubmittedCode_ == candidate.code) {
        timing_.decisionMs = timer_.elapsedMs(decisionStart, timer_.now());
        ui_.update(state_, config_, timing_);
        return;
    }

    state_ = RunState::Candidate;
    state_ = RunState::Confirmed;
    const LARGE_INTEGER dispatchStart = timer_.now();
    std::wstring error;
    if (input_.submit(candidate.code, config_, error)) {
        lastSubmittedCode_ = candidate.code;
    } else {
        MessageBoxW(ui_.window(), error.c_str(), L"VAL Invite", MB_OK | MB_ICONWARNING);
    }
    timing_.dispatchMs = timer_.elapsedMs(dispatchStart, timer_.now());
    timing_.decisionMs = timer_.elapsedMs(decisionStart, dispatchStart);
    state_ = RunState::Observing;
    ui_.update(state_, config_, timing_);
}

void App::persistCalibration() {
    std::wstring error;
    if (!configStore_.save(config_, error)) {
        MessageBoxW(ui_.window(), error.c_str(), L"VAL Invite", MB_OK | MB_ICONWARNING);
    }
}

} // namespace valinvite
