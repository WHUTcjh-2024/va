#include "ui.hpp"

#include <string>

namespace valinvite {
namespace {
constexpr wchar_t kWindowClass[] = L"VALInviteWindow";
constexpr int kStartButtonId = 1001;
constexpr int kStopButtonId = 1002;

const wchar_t* stateText(RunState state) noexcept {
    switch (state) {
    case RunState::Setup: return L"Setup";
    case RunState::Armed: return L"Armed";
    case RunState::Observing: return L"Observing";
    case RunState::Candidate: return L"Candidate";
    case RunState::Confirmed: return L"Confirmed";
    case RunState::Stopped: return L"Stopped";
    }
    return L"Unknown";
}
} // namespace

bool Ui::create(HINSTANCE instance, std::wstring& error) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"注册主窗口失败";
        return false;
    }
    window_ = CreateWindowExW(0, kWindowClass, L"VAL Invite V1", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 300, nullptr, nullptr, instance, nullptr);
    if (!window_) {
        error = L"创建主窗口失败";
        return false;
    }
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
        18, 18, 510, 185, window_, nullptr, instance, nullptr);
    startButton_ = CreateWindowExW(0, L"BUTTON", L"START (F10)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        18, 215, 120, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartButtonId)), instance, nullptr);
    stopButton_ = CreateWindowExW(0, L"BUTTON", L"STOP (F11)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 215, 120, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopButtonId)), instance, nullptr);
    if (!status_ || !startButton_ || !stopButton_) {
        error = L"创建界面控件失败";
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    return true;
}

HWND Ui::window() const noexcept { return window_; }

void Ui::update(RunState state, const Config& config, const TimingSnapshot& timing) {
    if (!window_) return;
    const std::wstring title = std::wstring{L"VAL Invite V1 | "} + stateText(state);
    SetWindowTextW(window_, title.c_str());
    const std::wstring status = std::wstring{L"状态："} + stateText(state)
        + L"\r\n\r\nInput：" + std::to_wstring(config.inputPoint.x) + L", " + std::to_wstring(config.inputPoint.y) + L"  [F8 校准]"
        + L"\r\nJoin：" + std::to_wstring(config.joinPoint.x) + L", " + std::to_wstring(config.joinPoint.y) + L"  [F9 校准]"
        + L"\r\n提交方式：" + (config.submitMode == SubmitMode::Enter ? L"Enter" : L"Click Join")
        + L"\r\nRecognition：" + std::to_wstring(timing.recognitionMs) + L" ms"
        + L"\r\nDecision：" + std::to_wstring(timing.decisionMs) + L" ms"
        + L"\r\nDispatch：" + std::to_wstring(timing.dispatchMs) + L" ms";
    SetWindowTextW(status_, status.c_str());
    EnableWindow(startButton_, state == RunState::Setup || state == RunState::Stopped);
    EnableWindow(stopButton_, state != RunState::Setup && state != RunState::Stopped);
}

LRESULT CALLBACK Ui::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        const WORD controlId = LOWORD(wParam);
        if (controlId == kStartButtonId) {
            PostMessageW(window, kCommandMessage, kStartCommand, 0);
            return 0;
        }
        if (controlId == kStopButtonId) {
            PostMessageW(window, kCommandMessage, kStopCommand, 0);
            return 0;
        }
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace valinvite
