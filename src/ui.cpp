#include "ui.hpp"

#include <iterator>
#include <string>

namespace valinvite {
namespace {
constexpr wchar_t kWindowClass[] = L"VALInviteWindow";
constexpr int kStartButtonId = 1001, kStopButtonId = 1002, kRefreshButtonId = 1003, kRoiButtonId = 1004;
struct WindowEnumerationContext final {
    HWND combo{};
    HWND excluded{};
};
const wchar_t* stateText(RunState state) noexcept { switch (state) { case RunState::Setup: return L"待配置"; case RunState::Armed: return L"识别中"; case RunState::Observing: return L"观察中"; case RunState::Candidate: return L"等待确认"; case RunState::Confirmed: return L"已提交"; case RunState::Stopped: return L"已停止"; } return L"未知"; }
BOOL CALLBACK addWindow(HWND window, LPARAM value) { const auto& context = *reinterpret_cast<const WindowEnumerationContext*>(value); if (window == context.excluded || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE; wchar_t title[256]{}; if (!GetWindowTextW(window, title, static_cast<int>(std::size(title)))) return TRUE; const auto index = SendMessageW(context.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title)); if (index != CB_ERR && index != CB_ERRSPACE) SendMessageW(context.combo, CB_SETITEMDATA, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(window)); return TRUE; }
} // namespace

bool Ui::create(HINSTANCE instance, std::wstring& error) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = L"注册主窗口失败"; return false; }
    window_ = CreateWindowExW(0, kWindowClass, L"VAL Invite", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 560, 285, nullptr, nullptr, instance, nullptr);
    if (!window_) { error = L"创建主窗口失败"; return false; }
    CreateWindowExW(0, L"STATIC", L"窗口", WS_CHILD | WS_VISIBLE, 18, 21, 60, 22, window_, nullptr, instance, nullptr);
    windowList_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 76, 17, 358, 260, window_, nullptr, instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 446, 17, 88, 27, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButtonId)), instance, nullptr);
    roiButton_ = CreateWindowExW(0, L"BUTTON", L"框选邀请码", WS_CHILD | WS_VISIBLE, 18, 58, 132, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRoiButtonId)), instance, nullptr);
    CreateWindowExW(0, L"STATIC", L"F8 输入框  ·  F9 加入按钮", WS_CHILD | WS_VISIBLE, 166, 65, 360, 22, window_, nullptr, instance, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 18, 104, 516, 84, window_, nullptr, instance, nullptr);
    startButton_ = CreateWindowExW(0, L"BUTTON", L"启动  F10", WS_CHILD | WS_VISIBLE, 18, 207, 112, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartButtonId)), instance, nullptr);
    stopButton_ = CreateWindowExW(0, L"BUTTON", L"停止  F11", WS_CHILD | WS_VISIBLE, 142, 207, 112, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopButtonId)), instance, nullptr);
    if (!status_ || !startButton_ || !stopButton_ || !windowList_ || !roiButton_) { error = L"创建界面控件失败"; DestroyWindow(window_); window_ = nullptr; return false; }
    EnumChildWindows(window_, [](HWND child, LPARAM) -> BOOL { SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return TRUE; }, 0);
    ShowWindow(window_, SW_SHOW); UpdateWindow(window_); return true;
}
HWND Ui::window() const noexcept { return window_; }
void Ui::refreshWindows() {
    const HWND previous = selectedWindow();
    SendMessageW(windowList_, CB_RESETCONTENT, 0, 0);
    const WindowEnumerationContext context{windowList_, window_};
    EnumWindows(addWindow, reinterpret_cast<LPARAM>(&context));
    const auto count = SendMessageW(windowList_, CB_GETCOUNT, 0, 0);
    LRESULT selection = count > 0 ? 0 : CB_ERR;
    for (LRESULT index = 0; index < count; ++index) {
        const auto candidate = reinterpret_cast<HWND>(
            SendMessageW(windowList_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)
        );
        if (candidate == previous) {
            selection = index;
            break;
        }
    }
    if (selection != CB_ERR) SendMessageW(windowList_, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
}
HWND Ui::selectedWindow() const noexcept { const auto index = SendMessageW(windowList_, CB_GETCURSEL, 0, 0); return index == CB_ERR ? nullptr : reinterpret_cast<HWND>(SendMessageW(windowList_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)); }
void Ui::update(
    RunState state,
    const Config& config,
    const TimingSnapshot&,
    const std::optional<Candidate>& candidate,
    std::wstring_view error
) {
    if (!window_) {
        return;
    }

    SetWindowTextW(
        window_,
        (
            std::wstring{L"VAL Invite · "} + stateText(state)
        ).c_str()
    );

    const std::wstring code =
        candidate
            ? std::wstring(
                candidate->code.begin(),
                candidate->code.end()
            )
            : L"-";

    const bool roiReady = config.normalizedRoi.valid() || config.roi.valid();
    std::wstring status = std::wstring{L"状态  "} + stateText(state) +
        L"\r\n邀请码  " + code +
        L"\r\nROI  " + (roiReady ? L"已设置" : L"未设置");

    if (!error.empty()) {
        status +=
            L"\r\n提示：" +
            std::wstring(error);
    }

    SetWindowTextW(
        status_,
        status.c_str()
    );

    EnableWindow(
        startButton_,
        state == RunState::Setup ||
        state == RunState::Stopped
    );

    EnableWindow(
        stopButton_,
        state != RunState::Setup &&
        state != RunState::Stopped
    );

}

LRESULT CALLBACK Ui::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        const WORD id = LOWORD(wParam);
        WPARAM command =
            id == kStartButtonId ? kStartCommand :
            id == kStopButtonId ? kStopCommand :
            id == kRefreshButtonId ? kSelectWindowCommand :
            id == kRoiButtonId ? kSelectRoiCommand : 0;
        if (command) {
            PostMessageW(window, kCommandMessage, command, 0);
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
