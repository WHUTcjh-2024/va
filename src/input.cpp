#include "input.hpp"

#include <array>
#include <cstddef>

namespace {

constexpr LONG kAbsoluteMouseRange = 65535;

bool screenPointToAbsolute(const valinvite::Point& point, LONG& x, LONG& y) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 1 || height <= 1) return false;

    const auto scale = [](int coordinate, int origin, int extent) -> LONG {
        const auto numerator = static_cast<long long>(coordinate - origin) * kAbsoluteMouseRange;
        return static_cast<LONG>(numerator / static_cast<long long>(extent - 1));
    };
    x = scale(point.x, left, width);
    y = scale(point.y, top, height);
    return true;
}

bool appendClick(std::array<INPUT, 24>& inputs, std::size_t& count, const valinvite::Point& point) {
    LONG x{};
    LONG y{};
    if (!screenPointToAbsolute(point, x, y)) return false;
    const DWORD flags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    INPUT& move = inputs[count++];
    move.type = INPUT_MOUSE;
    move.mi.dx = x;
    move.mi.dy = y;
    move.mi.dwFlags = flags | MOUSEEVENTF_MOVE;
    INPUT& down = inputs[count++];
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = flags | MOUSEEVENTF_LEFTDOWN;
    INPUT& up = inputs[count++];
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = flags | MOUSEEVENTF_LEFTUP;
    return true;
}

void appendVirtualKey(std::array<INPUT, 24>& inputs, std::size_t& count, WORD key) {
    INPUT& down = inputs[count++];
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = key;
    INPUT& up = inputs[count++];
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = key;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
}

void appendUnicodeCharacter(std::array<INPUT, 24>& inputs, std::size_t& count, char character) {
    const WORD value = static_cast<WORD>(static_cast<unsigned char>(character));
    INPUT& down = inputs[count++];
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = value;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT& up = inputs[count++];
    up.type = INPUT_KEYBOARD;
    up.ki.wScan = value;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
}

} // namespace

namespace valinvite {

bool InputDispatcher::submit(std::string_view code, const Config& config, std::wstring& error) const {
    if (code.size() != 6) {
        error = L"邀请码必须为六码";
        return false;
    }
    if (config.inputPoint.x == 0 && config.inputPoint.y == 0) {
        error = L"输入框坐标尚未校准";
        return false;
    }
    if (config.submitMode == SubmitMode::ClickJoin && config.joinPoint.x == 0 && config.joinPoint.y == 0) {
        error = L"Join 坐标尚未校准";
        return false;
    }

    LONG absoluteX{};
    LONG absoluteY{};
    if (!screenPointToAbsolute(config.inputPoint, absoluteX, absoluteY)
        || (config.submitMode == SubmitMode::ClickJoin && !screenPointToAbsolute(config.joinPoint, absoluteX, absoluteY))) {
        error = L"无法读取虚拟桌面尺寸";
        return false;
    }

    std::array<INPUT, 24> inputs{};
    std::size_t count{};
    if (!appendClick(inputs, count, config.inputPoint)) {
        error = L"无法转换输入框屏幕坐标";
        return false;
    }
    INPUT& controlDown = inputs[count++];
    controlDown.type = INPUT_KEYBOARD;
    controlDown.ki.wVk = VK_CONTROL;
    appendVirtualKey(inputs, count, 'A');
    INPUT& controlUp = inputs[count++];
    controlUp.type = INPUT_KEYBOARD;
    controlUp.ki.wVk = VK_CONTROL;
    controlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    for (const char character : code) appendUnicodeCharacter(inputs, count, character);
    if (config.submitMode == SubmitMode::Enter) {
        appendVirtualKey(inputs, count, VK_RETURN);
    } else {
        if (!appendClick(inputs, count, config.joinPoint)) {
            error = L"无法转换 Join 屏幕坐标";
            return false;
        }
    }

    const UINT sent = SendInput(static_cast<UINT>(count), inputs.data(), sizeof(INPUT));
    if (sent != static_cast<UINT>(count)) {
        error = L"SendInput 未完整发送（" + std::to_wstring(sent) + L"/" + std::to_wstring(count)
            + L"，错误 " + std::to_wstring(GetLastError()) + L"）";
        return false;
    }
    return true;
}

} // namespace valinvite
