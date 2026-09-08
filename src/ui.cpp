#include "ui.hpp"

#include <algorithm>
#include <iterator>
#include <string>

namespace valinvite {
namespace {
constexpr wchar_t kWindowClass[] = L"VALInviteWindow";
constexpr wchar_t kRoiClass[] = L"VALInviteRoiPicker";
constexpr int kStartButtonId = 1001, kStopButtonId = 1002, kRefreshButtonId = 1003, kRoiButtonId = 1004;
const wchar_t* stateText(RunState state) noexcept { switch (state) { case RunState::Setup: return L"Setup"; case RunState::Armed: return L"Armed"; case RunState::Observing: return L"Observing"; case RunState::Candidate: return L"Candidate"; case RunState::Confirmed: return L"Confirmed"; case RunState::Stopped: return L"Stopped"; } return L"Unknown"; }
BOOL CALLBACK addWindow(HWND window, LPARAM value) { auto combo = reinterpret_cast<HWND>(value); if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE; wchar_t title[256]{}; if (!GetWindowTextW(window, title, static_cast<int>(std::size(title)))) return TRUE; const auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title)); if (index != CB_ERR && index != CB_ERRSPACE) SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(window)); return TRUE; }
LRESULT CALLBACK roiProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) { if (message == WM_PAINT) { PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps); const auto* points = reinterpret_cast<const POINT*>(GetWindowLongPtrW(window, GWLP_USERDATA)); if (points) { HPEN pen = CreatePen(PS_SOLID, 2, RGB(40, 220, 180)); HGDIOBJ oldPen = SelectObject(dc, pen); HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH)); Rectangle(dc, points[0].x, points[0].y, points[1].x, points[1].y); SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen); } EndPaint(window, &ps); return 0; } return DefWindowProcW(window, message, wParam, lParam); }
} // namespace

bool Ui::create(HINSTANCE instance, std::wstring& error) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = windowProc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = L"注册主窗口失败"; return false; }
    window_ = CreateWindowExW(0, kWindowClass, L"VAL Invite V1", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 680, 430, nullptr, nullptr, instance, nullptr);
    if (!window_) { error = L"创建主窗口失败"; return false; }
    CreateWindowExW(0, L"STATIC", L"捕获窗口：", WS_CHILD | WS_VISIBLE, 18, 18, 88, 24, window_, nullptr, instance, nullptr);
    windowList_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 106, 15, 420, 300, window_, nullptr, instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"刷新窗口", WS_CHILD | WS_VISIBLE, 538, 15, 110, 26, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButtonId)), instance, nullptr);
    roiButton_ = CreateWindowExW(0, L"BUTTON", L"框选 ROI", WS_CHILD | WS_VISIBLE, 18, 54, 120, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRoiButtonId)), instance, nullptr);
    preview_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"ROI Preview", WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE, 470, 56, 178, 118, window_, nullptr, instance, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 18, 98, 435, 235, window_, nullptr, instance, nullptr);
    startButton_ = CreateWindowExW(0, L"BUTTON", L"START (F10)", WS_CHILD | WS_VISIBLE, 18, 350, 120, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartButtonId)), instance, nullptr);
    stopButton_ = CreateWindowExW(0, L"BUTTON", L"STOP (F11)", WS_CHILD | WS_VISIBLE, 150, 350, 120, 32, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopButtonId)), instance, nullptr);
    if (!status_ || !startButton_ || !stopButton_ || !windowList_ || !roiButton_ || !preview_) { error = L"创建界面控件失败"; DestroyWindow(window_); window_ = nullptr; return false; }
    ShowWindow(window_, SW_SHOW); UpdateWindow(window_); return true;
}
HWND Ui::window() const noexcept { return window_; }
void Ui::refreshWindows() { SendMessageW(windowList_, CB_RESETCONTENT, 0, 0); EnumWindows(addWindow, reinterpret_cast<LPARAM>(windowList_)); if (SendMessageW(windowList_, CB_GETCOUNT, 0, 0) > 0) SendMessageW(windowList_, CB_SETCURSEL, 0, 0); }
HWND Ui::selectedWindow() const noexcept { const auto index = SendMessageW(windowList_, CB_GETCURSEL, 0, 0); return index == CB_ERR ? nullptr : reinterpret_cast<HWND>(SendMessageW(windowList_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0)); }
bool Ui::selectRoi(Rect& roi, std::wstring& error) {
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = roiProc; wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window_, GWLP_HINSTANCE)); wc.hCursor = LoadCursor(nullptr, IDC_CROSS); wc.lpszClassName = kRoiClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = L"无法创建 ROI 选择层"; return false; }
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN), width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, kRoiClass, L"", WS_POPUP, left, top, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!overlay) { error = L"无法显示 ROI 选择层"; return false; } SetLayeredWindowAttributes(overlay, 0, 1, LWA_ALPHA); ShowWindow(overlay, SW_SHOW); SetCapture(overlay);
    POINT points[2]{}; bool drawing = false, accepted = false;
    while (IsWindow(overlay)) { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { if (message.message == WM_QUIT) { PostQuitMessage(static_cast<int>(message.wParam)); break; } TranslateMessage(&message); DispatchMessageW(&message); } POINT cursor{}; GetCursorPos(&cursor); cursor.x -= left; cursor.y -= top; if ((GetAsyncKeyState(VK_ESCAPE) & 1) != 0) break; if (!drawing && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) { points[0] = cursor; points[1] = cursor; drawing = true; SetWindowLongPtrW(overlay, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(points)); } if (drawing) { points[1] = cursor; InvalidateRect(overlay, nullptr, TRUE); if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) { accepted = std::abs(points[1].x - points[0].x) > 4 && std::abs(points[1].y - points[0].y) > 4; break; } } Sleep(8); }
    ReleaseCapture(); DestroyWindow(overlay); if (!accepted) { error = L"已取消 ROI 框选"; return false; } roi = {left + std::min(points[0].x, points[1].x), top + std::min(points[0].y, points[1].y), std::abs(points[1].x - points[0].x), std::abs(points[1].y - points[0].y)}; return true;
}
void Ui::update(
    RunState state,
    const Config& config,
    const TimingSnapshot& timing,
    const std::optional<Candidate>& candidate,
    std::wstring_view error
) {
    if (!window_) {
        return;
    }

    SetWindowTextW(
        window_,
        (
            std::wstring{
                L"VAL Invite V1 | "
            } +
            stateText(state)
        ).c_str()
    );

    const std::wstring code =
        candidate
            ? std::wstring(
                candidate->code.begin(),
                candidate->code.end()
            )
            : L"-";

    std::wstring status =
        std::wstring{L"状态："} +
        stateText(state) +

        L"\r\nROI：" +
        std::to_wstring(config.roi.x) +
        L", " +
        std::to_wstring(config.roi.y) +
        L"  " +
        std::to_wstring(config.roi.width) +
        L"×" +
        std::to_wstring(config.roi.height) +

        L"\r\nInput：" +
        std::to_wstring(
            config.inputPoint.x
        ) +
        L", " +
        std::to_wstring(
            config.inputPoint.y
        ) +
        L" [F8]  Join：" +
        std::to_wstring(
            config.joinPoint.x
        ) +
        L", " +
        std::to_wstring(
            config.joinPoint.y
        ) +
        L" [F9]" +

        L"\r\n识别码：" +
        code +

        L"  置信：" +
        (
            candidate &&
            candidate->highConfidence
                ? L"高"
                : L"待确认"
        ) +

        L"\r\n耗时 Recognition " +
        std::to_wstring(
            timing.recognitionMs
        ) +
        L" ms | Decision " +
        std::to_wstring(
            timing.decisionMs
        ) +
        L" ms | SendInput " +
        std::to_wstring(
            timing.dispatchMs
        ) +
        L" ms";

    if (!error.empty()) {
        status +=
            L"\r\n提示：" +
            std::wstring(error);
    }

    if (state == RunState::Setup ||
        state == RunState::Stopped) {
        status +=
            L"\r\n\r\n提示：请尽量只框住六码文字，四周保留少量边距。";
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

    // Preview 只更新 10 FPS。
    // 不允许每个识别帧都创建 GDI bitmap。
    static ULONGLONG
        lastPreviewTick = 0;

    const ULONGLONG now =
        GetTickCount64();

    if (!config.roi.valid() ||
        now - lastPreviewTick < 100) {
        return;
    }

    const HWND sourceWindow =
        selectedWindow();

    if (!sourceWindow ||
        !IsWindow(sourceWindow)) {
        return;
    }

    POINT clientOrigin{0, 0};

    if (!ClientToScreen(
            sourceWindow,
            &clientOrigin)) {
        return;
    }

    const int sourceX =
        clientOrigin.x +
        config.roi.x;

    const int sourceY =
        clientOrigin.y +
        config.roi.y;

    HDC screen =
        GetDC(nullptr);

    if (!screen) {
        return;
    }

    HDC memory =
        CreateCompatibleDC(screen);

    if (!memory) {
        ReleaseDC(nullptr, screen);
        return;
    }

    constexpr int previewWidth = 174;
    constexpr int previewHeight = 114;

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            screen,
            previewWidth,
            previewHeight
        );

    if (!bitmap) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return;
    }

    HGDIOBJ old =
        SelectObject(
            memory,
            bitmap
        );

    SetStretchBltMode(
        memory,
        COLORONCOLOR
    );

    StretchBlt(
        memory,
        0,
        0,
        previewWidth,
        previewHeight,

        screen,
        sourceX,
        sourceY,
        config.roi.width,
        config.roi.height,

        SRCCOPY
    );

    SelectObject(
        memory,
        old
    );

    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    const auto oldBitmap =
        reinterpret_cast<HBITMAP>(
            SendMessageW(
                preview_,
                STM_SETIMAGE,
                IMAGE_BITMAP,
                reinterpret_cast<LPARAM>(
                    bitmap
                )
            )
        );

    if (oldBitmap) {
        DeleteObject(oldBitmap);
    }

    lastPreviewTick = now;
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
