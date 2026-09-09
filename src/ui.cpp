#include "ui.hpp"

#include <iterator>
#include <string>
#include <utility>

namespace valinvite {
namespace {
constexpr wchar_t kWindowClass[] = L"VALInviteWindow";
constexpr int kLogicalWidth = 720;
constexpr int kLogicalHeight = 420;
constexpr int kStartButtonId = 1001, kStopButtonId = 1002, kRefreshButtonId = 1003, kRoiButtonId = 1004,
    kInputButtonId = 1005, kJoinButtonId = 1006;
struct WindowEnumerationContext final {
    HWND combo{};
    HWND excluded{};
};
const wchar_t* stateText(RunState state) noexcept { switch (state) { case RunState::Setup: return L"待配置"; case RunState::Armed: return L"识别中"; case RunState::Observing: return L"观察中"; case RunState::Candidate: return L"等待确认"; case RunState::Confirmed: return L"已提交"; case RunState::Stopped: return L"已停止"; } return L"未知"; }
BOOL CALLBACK addWindow(HWND window, LPARAM value) {
    const auto& context = *reinterpret_cast<const WindowEnumerationContext*>(value);
    if (window == context.excluded || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    RECT client{};
    if ((style & (WS_CHILD | WS_DISABLED | WS_CAPTION)) != WS_CAPTION || !GetClientRect(window, &client)
        || client.right - client.left < 320 || client.bottom - client.top < 180) {
        return TRUE;
    }
    wchar_t title[256]{};
    if (!GetWindowTextW(window, title, static_cast<int>(std::size(title)))) return TRUE;
    const auto index = SendMessageW(context.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title));
    if (index != CB_ERR && index != CB_ERRSPACE) {
        SendMessageW(context.combo, CB_SETITEMDATA, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(window));
    }
    return TRUE;
}
} // namespace

bool Ui::create(HINSTANCE instance, std::wstring& error) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = L"注册主窗口失败"; return false; }
    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    window_ = CreateWindowExW(0, kWindowClass, L"VAL Invite", windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        kLogicalWidth, kLogicalHeight, nullptr, nullptr, instance, this);
    if (!window_) { error = L"创建主窗口失败"; return false; }
    const UINT dpi = GetDpiForWindow(window_);
    const auto scaled = [dpi](int value) noexcept { return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); };
    RECT desiredClient{0, 0, scaled(kLogicalWidth), scaled(kLogicalHeight)};
    if (AdjustWindowRectExForDpi(&desiredClient, windowStyle, FALSE, 0, GetDpiForWindow(window_))) {
        SetWindowPos(window_, nullptr, 0, 0, desiredClient.right - desiredClient.left, desiredClient.bottom - desiredClient.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    const auto createControl = [&](const wchar_t* className, const wchar_t* text, DWORD style,
                                   int x, int y, int width, int height, int id = 0) {
        const HWND control = CreateWindowExW(0, className, text, style, 0, 0, 0, 0,
            window_, id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, instance, nullptr);
        if (control) controls_.push_back({control, x, y, width, height});
        return control;
    };

    createControl(L"BUTTON", L"捕获来源", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 14, 680, 132);
    createControl(L"STATIC", L"显示邀请码的窗口", WS_CHILD | WS_VISIBLE, 40, 48, 142, 24);
    windowList_ = createControl(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 194, 42, 350, 240);
    createControl(L"BUTTON", L"刷新列表", WS_CHILD | WS_VISIBLE, 558, 42, 122, 34, kRefreshButtonId);
    roiButton_ = createControl(L"BUTTON", L"框选邀请码区域", WS_CHILD | WS_VISIBLE, 40, 92, 176, 36, kRoiButtonId);
    createControl(L"STATIC", L"先选择上方窗口，再框住六码文字", WS_CHILD | WS_VISIBLE, 236, 100, 430, 24);

    createControl(L"BUTTON", L"提交位置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 160, 680, 82);
    inputButton_ = createControl(L"BUTTON", L"框选邀请码输入框", WS_CHILD | WS_VISIBLE, 40, 190, 206, 38, kInputButtonId);
    joinButton_ = createControl(L"BUTTON", L"框选加入按钮", WS_CHILD | WS_VISIBLE, 264, 190, 190, 38, kJoinButtonId);
    createControl(L"STATIC", L"可选；不选则按 Enter", WS_CHILD | WS_VISIBLE, 474, 198, 206, 26);

    createControl(L"BUTTON", L"运行状态", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 20, 256, 680, 94);
    status_ = createControl(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 40, 282, 640, 60);

    startButton_ = createControl(L"BUTTON", L"启动  F10", WS_CHILD | WS_VISIBLE, 20, 370, 154, 38, kStartButtonId);
    stopButton_ = createControl(L"BUTTON", L"停止  F11", WS_CHILD | WS_VISIBLE, 190, 370, 154, 38, kStopButtonId);
    createControl(L"STATIC", L"启动后保持邀请码来源窗口可见", WS_CHILD | WS_VISIBLE, 376, 378, 310, 24);
    if (!status_ || !startButton_ || !stopButton_ || !windowList_ || !roiButton_ || !inputButton_ || !joinButton_) { error = L"创建界面控件失败"; DestroyWindow(window_); window_ = nullptr; return false; }
    applyDpi(dpi);
    ShowWindow(window_, SW_SHOW); UpdateWindow(window_); return true;
}
Ui::~Ui() { if (uiFont_) DeleteObject(uiFont_); }
HWND Ui::window() const noexcept { return window_; }

void Ui::layoutControls(UINT dpi) const noexcept {
    for (const ControlLayout& control : controls_) {
        const auto scale = [dpi](int value) noexcept {
            return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
        };
        MoveWindow(
            control.window,
            scale(control.x),
            scale(control.y),
            scale(control.width),
            scale(control.height),
            FALSE
        );
    }
}

void Ui::replaceFont(UINT dpi) {
    HFONT next = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!next) return;

    for (const ControlLayout& control : controls_) {
        SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
    }
    const HFONT previous = std::exchange(uiFont_, next);
    if (previous) DeleteObject(previous);
}

void Ui::applyDpi(UINT dpi) {
    if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;
    layoutControls(dpi);
    replaceFont(dpi);
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}
void Ui::refreshWindows() {
    const HWND previous = selectedWindow();
    SendMessageW(windowList_, CB_RESETCONTENT, 0, 0);
    const WindowEnumerationContext context{windowList_, window_};
    EnumWindows(addWindow, reinterpret_cast<LPARAM>(&context));
    const auto count = SendMessageW(windowList_, CB_GETCOUNT, 0, 0);
    LRESULT selection = count > 0 ? 0 : CB_ERR;
    LRESULT benchmarkSelection = CB_ERR;
    for (LRESULT index = 0; index < count; ++index) {
        const auto candidate = reinterpret_cast<HWND>(
            SendMessageW(windowList_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)
        );
        if (candidate == previous) {
            selection = index;
            break;
        }
        wchar_t title[256]{};
        GetWindowTextW(candidate, title, static_cast<int>(std::size(title)));
        if (benchmarkSelection == CB_ERR && std::wstring_view{title}.find(L"VAL Invite Benchmark Stream") != std::wstring_view::npos) {
            benchmarkSelection = index;
        }
    }
    if (previous == nullptr && benchmarkSelection != CB_ERR) selection = benchmarkSelection;
    if (selection != CB_ERR) SendMessageW(windowList_, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
}
HWND Ui::selectedWindow() const noexcept {
    const auto index = SendMessageW(windowList_, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return nullptr;
    const auto window = reinterpret_cast<HWND>(
        SendMessageW(windowList_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)
    );
    return IsWindow(window) ? window : nullptr;
}
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
            std::wstring{L"VAL Invite"}
        ).c_str()
    );

    const std::wstring code =
        candidate
            ? std::wstring(
                candidate->code.begin(),
                candidate->code.end()
            )
            : L"—";

    const bool roiReady = config.normalizedRoi.valid() || config.roi.valid();
    const bool inputReady = config.inputPoint.x != 0 || config.inputPoint.y != 0;
    const bool joinReady = config.joinPoint.x != 0 || config.joinPoint.y != 0;
    std::wstring status = std::wstring{L"状态："} + stateText(state) +
        L"    识别：" + code +
        L"\r\nROI " + (roiReady ? L"已设置" : L"未设置") +
        L"    输入框 " + (inputReady ? L"已设置" : L"未设置") +
        L"    加入按钮 " + (joinReady ? L"已设置" : L"未设置") +
        L"    提交 " + (config.submitMode == SubmitMode::ClickJoin ? L"点击按钮" : L"Enter");

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
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* ui = reinterpret_cast<Ui*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_DPICHANGED) {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (ui) ui->applyDpi(HIWORD(wParam));
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) {
        const WORD id = LOWORD(wParam);
        WPARAM command =
            id == kStartButtonId ? kStartCommand :
            id == kStopButtonId ? kStopCommand :
            id == kRefreshButtonId ? kSelectWindowCommand :
            id == kRoiButtonId ? kSelectRoiCommand :
            id == kInputButtonId ? kSelectInputCommand :
            id == kJoinButtonId ? kSelectJoinCommand : 0;
        if (command) {
            PostMessageW(window, kCommandMessage, command, 0);
            return 0;
        }
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace valinvite
