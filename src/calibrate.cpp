#include "calibrate.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cstdlib>

namespace valinvite {
namespace {
constexpr wchar_t kOverlayClass[] = L"VALInviteRoiOverlay";
struct OverlayState final { POINT start{}, current{}; bool dragging{}, accepted{}, cancelled{}; };
class ThreadDpiContext final { public: ThreadDpiContext() noexcept : old_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {} ~ThreadDpiContext() { if (old_) SetThreadDpiAwarenessContext(old_); } private: DPI_AWARENESS_CONTEXT old_{}; };
bool clientArea(HWND window, Rect& out) noexcept { RECT client{}; POINT origin{}; if (!GetClientRect(window, &client) || !ClientToScreen(window, &origin)) return false; out = {origin.x, origin.y, client.right - client.left, client.bottom - client.top}; return out.valid(); }
bool capturable(HWND window) noexcept { if (!IsWindow(window) || !IsWindowVisible(window) || IsIconic(window) || GetWindow(window, GW_OWNER)) return false; Rect area{}; return (GetWindowLongPtrW(window, GWL_STYLE) & (WS_CHILD | WS_DISABLED)) == 0 && clientArea(window, area); }
LRESULT CALLBACK overlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<OverlayState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams)); return TRUE;
    case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_CROSS)); return TRUE;
    case WM_LBUTTONDOWN: if (state) { state->start = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; state->current = state->start; state->dragging = true; SetCapture(window); InvalidateRect(window, nullptr, FALSE); } return 0;
    case WM_MOUSEMOVE: if (state && state->dragging) { state->current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; InvalidateRect(window, nullptr, FALSE); } return 0;
    case WM_LBUTTONUP: if (state && state->dragging) { state->current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; state->dragging = false; ReleaseCapture(); state->accepted = state->current.x != state->start.x && state->current.y != state->start.y; state->cancelled = !state->accepted; DestroyWindow(window); } return 0;
    case WM_RBUTTONUP: if (state) state->cancelled = true; DestroyWindow(window); return 0;
    case WM_KEYDOWN: if (wParam == VK_ESCAPE) { if (state) state->cancelled = true; DestroyWindow(window); return 0; } break;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps); RECT area{}; GetClientRect(window, &area); HBRUSH veil = CreateSolidBrush(RGB(0, 0, 0)); FillRect(dc, &area, veil); DeleteObject(veil); if (state && state->dragging) { RECT chosen{std::min(state->start.x, state->current.x), std::min(state->start.y, state->current.y), std::max(state->start.x, state->current.x), std::max(state->start.y, state->current.y)}; FrameRect(dc, &chosen, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))); } EndPaint(window, &ps); return 0; }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
bool registerOverlay(std::wstring& error) { WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = overlayProc; cls.hInstance = GetModuleHandleW(nullptr); cls.hCursor = LoadCursor(nullptr, IDC_CROSS); cls.lpszClassName = kOverlayClass; if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { error = L"注册 ROI 框选窗口失败"; return false; } return true; }
} // namespace

std::vector<CaptureWindow> Calibrator::enumerateCaptureWindows() const {
    ThreadDpiContext dpi; std::vector<CaptureWindow> result;
    EnumWindows([](HWND window, LPARAM data) -> BOOL { if (!capturable(window)) return TRUE; const int length = GetWindowTextLengthW(window); if (length <= 0) return TRUE; std::wstring title(static_cast<size_t>(length) + 1, L'\0'); GetWindowTextW(window, title.data(), static_cast<int>(title.size())); title.resize(static_cast<size_t>(length)); Rect area{}; if (clientArea(window, area)) reinterpret_cast<std::vector<CaptureWindow>*>(data)->push_back({window, std::move(title), area}); return TRUE; }, reinterpret_cast<LPARAM>(&result));
    return result;
}
bool Calibrator::selectCaptureWindow(Config& config, HWND window, std::wstring& error) const {
    ThreadDpiContext dpi; if (!capturable(window)) { error = L"请选择一个可见的顶层直播窗口"; return false; } const int length = GetWindowTextLengthW(window); std::wstring title(static_cast<size_t>(length) + 1, L'\0'); GetWindowTextW(window, title.data(), static_cast<int>(title.size())); title.resize(static_cast<size_t>(length)); config.captureWindowTitle = std::move(title); config.roi = {}; return true;
}
bool Calibrator::selectCaptureWindowAtCursor(Config& config, std::wstring& error) const { ThreadDpiContext dpi; POINT point{}; if (!GetCursorPos(&point)) { error = L"无法读取鼠标位置"; return false; } return selectCaptureWindow(config, GetAncestor(WindowFromPoint(point), GA_ROOT), error); }
bool Calibrator::selectRoi(Config& config, HWND window, std::wstring& error) const {
    ThreadDpiContext dpi; Rect area{}; if (!capturable(window) || !clientArea(window, area)) { error = L"直播窗口无效或没有可用 Client Area"; return false; } if (!registerOverlay(error)) return false; OverlayState state{}; HWND overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClass, L"", WS_POPUP, area.x, area.y, area.width, area.height, nullptr, nullptr, GetModuleHandleW(nullptr), &state); if (!overlay) { error = L"创建 ROI 框选层失败"; return false; } SetLayeredWindowAttributes(overlay, 0, 96, LWA_ALPHA); ShowWindow(overlay, SW_SHOWNOACTIVATE); SetForegroundWindow(overlay); MSG message{}; while (IsWindow(overlay) && GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); } if (message.message == WM_QUIT) PostQuitMessage(static_cast<int>(message.wParam)); if (!state.accepted || state.cancelled) { error = L"已取消 ROI 框选"; return false; } config.roi = {std::min(state.start.x, state.current.x), std::min(state.start.y, state.current.y), std::abs(state.current.x - state.start.x), std::abs(state.current.y - state.start.y)}; return true;
}
void Calibrator::recordInputPoint(Config& config) const noexcept { POINT point{}; if (GetCursorPos(&point)) config.inputPoint = {point.x, point.y}; }
void Calibrator::recordJoinPoint(Config& config) const noexcept { POINT point{}; if (GetCursorPos(&point)) config.joinPoint = {point.x, point.y}; }
} // namespace valinvite
