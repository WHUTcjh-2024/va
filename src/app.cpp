#include "app.hpp"

#include <shlobj.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace valinvite {
namespace {
constexpr int kHotkeyStart = 1;
constexpr int kHotkeyStop = 2;
constexpr UINT kRecognizedCandidateMessage = WM_APP + 2;

struct RecognizedCandidate final {
    Candidate candidate;
    double recognitionMs{};
};

std::filesystem::path localConfigPath() {
    PWSTR rawPath{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &rawPath))) return {};
    const std::filesystem::path path{rawPath};
    CoTaskMemFree(rawPath);
    return path / L"VALInvite" / L"config.json";
}
}

App::App(HINSTANCE instance)
    : instance_{instance},
      configStore_{localConfigPath()} {}
App::~App() {
    acceptingFrames_.store(false, std::memory_order_release);
    capture_.setFrameCallback({});
    capture_.stop();
}

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
    RegisterHotKey(ui_.window(), kHotkeyStart, 0, VK_F10);
    RegisterHotKey(ui_.window(), kHotkeyStop, 0, VK_F11);
    ui_.refreshWindows();
    applyPerformancePolicy();
    ui_.update(state_, config_, timing_, std::nullopt, lastError_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY) handleHotkey(message.wParam);
        if (message.message == Ui::kCommandMessage) {
            if (message.wParam == Ui::kStartCommand) start();
            if (message.wParam == Ui::kStopCommand) stop();
            if (message.wParam == Ui::kSelectWindowCommand) ui_.refreshWindows();
            if (message.wParam == Ui::kSelectRoiCommand) {
                std::wstring roiError;
                if (calibrator_.selectRoi(config_, ui_.selectedWindow(), roiError)) persistCalibration();
                else if (!roiError.empty()) reportWarning(roiError);
                ui_.update(state_, config_, timing_, std::nullopt, lastError_);
            }
            if (message.wParam == Ui::kSelectInputCommand) {
                if (state_ != RunState::Setup && state_ != RunState::Stopped) stop();
                std::wstring selectionError;
                if (calibrator_.selectInputPoint(config_, selectionError)) persistCalibration();
                else if (!selectionError.empty()) reportWarning(selectionError);
                ui_.update(state_, config_, timing_, std::nullopt, lastError_);
            }
            if (message.wParam == Ui::kSelectJoinCommand) {
                if (state_ != RunState::Setup && state_ != RunState::Stopped) stop();
                std::wstring selectionError;
                if (calibrator_.selectJoinPoint(config_, selectionError)) {
                    config_.submitMode = SubmitMode::ClickJoin;
                    persistCalibration();
                } else if (!selectionError.empty()) {
                    reportWarning(selectionError);
                }
                ui_.update(state_, config_, timing_, std::nullopt, lastError_);
            }
        }
        if (message.message == kRecognizedCandidateMessage) {
            std::unique_ptr<RecognizedCandidate> result{reinterpret_cast<RecognizedCandidate*>(message.lParam)};
            if (result) processRecognizedCandidate(std::move(result->candidate), result->recognitionMs);
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    UnregisterHotKey(ui_.window(), kHotkeyStart);
    UnregisterHotKey(ui_.window(), kHotkeyStop);
    return static_cast<int>(message.wParam);
}

void App::start() {
    if (state_ != RunState::Setup &&
        state_ != RunState::Stopped) {
        return;
    }

    if (!recognizer_.ready()) {
        reportWarning(
            L"识别模板未加载，无法启动"
        );
        return;
    }

    const HWND source =
        ui_.selectedWindow();

    if (!source ||
        !IsWindow(source)) {
        reportWarning(
            L"请选择有效的直播窗口"
        );
        return;
    }

    std::wstring error;

    Rect activeRoi{};
    if (!calibrator_.resolveRoi(config_, source, activeRoi, error)) {
        reportWarning(error);
        return;
    }
    config_.roi = activeRoi;

    recognizer_.setConfig(
        config_.recognition
    );

    // Hot Path buffer 只在 START 时分配。
    const std::size_t graySize =
        static_cast<std::size_t>(
            activeRoi.width
        ) *
        static_cast<std::size_t>(
            activeRoi.height
        );

    grayBuffer_.assign(
        graySize,
        0
    );

    capture_.setFrameCallback(
        [this](
            const BgraRoiFrame& frame
        ) {
            onFrame(frame);
        }
    );

    if (!capture_.start(
            source,
            activeRoi,
            error)) {

        capture_.setFrameCallback({});

        reportWarning(error);

        ui_.update(
            state_,
            config_,
            timing_,
            std::nullopt,
            lastError_
        );

        return;
    }

    lastError_.clear();

    acceptingFrames_.store(
        true,
        std::memory_order_release
    );

    state_ =
        RunState::Armed;

    pendingCandidate_.reset();
    lastSubmittedCode_.reset();

    ui_.update(
        state_,
        config_,
        timing_,
        std::nullopt,
        lastError_
    );
}

void App::stop() {
    acceptingFrames_.store(false, std::memory_order_release);
    capture_.setFrameCallback({});
    capture_.stop();
    state_ = RunState::Stopped;
    pendingCandidate_.reset();
    ui_.update(state_, config_, timing_, std::nullopt, lastError_);
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
}

void App::onCandidate(const Candidate& candidate) {
    if (state_ != RunState::Armed && state_ != RunState::Confirmed) return;

    const LARGE_INTEGER decisionStart = timer_.now();

    // idle/no-code: the ROI shows no readable code (few or no slots carry ink).
    // A blank ROI still hard-selects six characters, so structureValid stays
    // true and cannot express "code gone". Clear the one-shot memory here so a
    // later code - even the identical one re-broadcast - can submit again.
    if (!candidate.codeVisible) {
        pendingCandidate_.reset();
        lastSubmittedCode_.reset();
        timing_.decisionMs = timer_.elapsedMs(decisionStart, timer_.now());
        ui_.update(state_, config_, timing_, candidate, lastError_);
        return;
    }

    if (!recognizer_.shouldSubmit(candidate, pendingCandidate_)) {
        pendingCandidate_ = candidate.structureValid ? std::optional<std::string>{candidate.code} : std::nullopt;
        if (!candidate.structureValid) lastSubmittedCode_.reset();
        timing_.decisionMs = timer_.elapsedMs(decisionStart, timer_.now());
        ui_.update(state_, config_, timing_, candidate, lastError_);
        return;
    }
    if (lastSubmittedCode_ && *lastSubmittedCode_ == candidate.code) {
        timing_.decisionMs = timer_.elapsedMs(decisionStart, timer_.now());
        ui_.update(state_, config_, timing_, candidate, lastError_);
        return;
    }

    state_ = RunState::Confirmed;
    const LARGE_INTEGER dispatchStart = timer_.now();
    std::wstring error;
    if (input_.submit(candidate.code, config_, error)) {
        lastSubmittedCode_ = candidate.code;
    } else {
        reportWarning(error);
    }
    timing_.dispatchMs = timer_.elapsedMs(dispatchStart, timer_.now());
    timing_.decisionMs = timer_.elapsedMs(decisionStart, dispatchStart);
    ui_.update(state_, config_, timing_, candidate, lastError_);
}

void App::onFrame(
    const BgraRoiFrame& frame
) {
    if (!acceptingFrames_.load(
            std::memory_order_acquire)) {
        return;
    }

    if (frame.pixels == nullptr ||
        frame.width == 0 ||
        frame.height == 0 ||
        frame.rowPitch <
            frame.width * 4U) {
        return;
    }

    // WGC CreateFreeThreaded 的 FrameArrived
    // 不一定跑在 UI Thread。
    // 所以真正 Hot Thread 的优先级在这里设置。
    thread_local bool
        performanceConfigured = false;

    if (!performanceConfigured) {
        if (config_.highPriority) {
            SetThreadPriority(
                GetCurrentThread(),
                THREAD_PRIORITY_HIGHEST
            );
        }

        if (config_.cpuAffinity >= 0 &&
            config_.cpuAffinity <
                static_cast<int>(
                    sizeof(DWORD_PTR) * 8U
                )) {

            const DWORD_PTR mask =
                static_cast<DWORD_PTR>(1)
                << config_.cpuAffinity;

            SetThreadAffinityMask(
                GetCurrentThread(),
                mask
            );
        }

        performanceConfigured = true;
    }

    const std::size_t required =
        static_cast<std::size_t>(
            frame.width
        ) *
        static_cast<std::size_t>(
            frame.height
        );

    // 正常情况下 START 时已分配好。
    // 运行中绝不 resize，避免 Hot Path heap allocation。
    if (grayBuffer_.size() != required) {
        return;
    }

    const LARGE_INTEGER start =
        timer_.now();

    for (std::uint32_t y = 0;
         y < frame.height;
         ++y) {

        const auto* source =
            frame.pixels +
            static_cast<std::size_t>(y) *
                frame.rowPitch;

        auto* target =
            grayBuffer_.data() +
            static_cast<std::size_t>(y) *
                frame.width;

        for (std::uint32_t x = 0;
             x < frame.width;
             ++x) {

            const auto b =
                source[x * 4U];

            const auto g =
                source[x * 4U + 1U];

            const auto r =
                source[x * 4U + 2U];

            // BT.601 整数近似。
            target[x] =
                static_cast<std::uint8_t>(
                    (
                        29U * b +
                        150U * g +
                        77U * r +
                        128U
                    ) >> 8U
                );
        }
    }

    Candidate candidate =
        recognizer_.recognize(
            {
                grayBuffer_.data(),
                static_cast<int>(
                    frame.width
                ),
                static_cast<int>(
                    frame.height
                ),
                static_cast<std::ptrdiff_t>(
                    frame.width
                )
            }
        );

    auto* result =
        new RecognizedCandidate{
            std::move(candidate),
            timer_.elapsedMs(
                start,
                timer_.now()
            )
        };

    if (!PostMessageW(
            ui_.window(),
            kRecognizedCandidateMessage,
            0,
            reinterpret_cast<LPARAM>(
                result
            ))) {

        delete result;
    }
}

void App::processRecognizedCandidate(Candidate candidate, double recognitionMs) {
    if (!acceptingFrames_.load(std::memory_order_acquire)) return;
    timing_.recognitionMs = recognitionMs;
    onCandidate(candidate);
}

void App::persistCalibration() {
    std::wstring error;
    if (!configStore_.save(config_, error)) {
        MessageBoxW(ui_.window(), error.c_str(), L"VAL Invite", MB_OK | MB_ICONWARNING);
    }
}

void App::reportWarning(const std::wstring& message) {
    lastError_ = message;
    MessageBoxW(ui_.window(), message.c_str(), L"VAL Invite", MB_OK | MB_ICONWARNING);
}

void App::applyPerformancePolicy() {
    if (config_.highPriority && !SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
        lastError_ = L"无法设置线程最高优先级（错误 " + std::to_wstring(GetLastError()) + L"）";
    }
    if (config_.cpuAffinity >= 0 && config_.cpuAffinity < static_cast<int>(sizeof(DWORD_PTR) * 8U)) {
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << config_.cpuAffinity;
        if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
            lastError_ = L"无法设置 CPU 亲和性（错误 " + std::to_wstring(GetLastError()) + L"）";
        }
    }
}

} // namespace valinvite
