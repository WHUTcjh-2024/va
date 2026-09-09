#include "input.hpp"

#include <array>
#include <cstddef>

namespace {

constexpr LONG kAbsoluteMouseRange = 65535;
constexpr std::size_t kMaximumInputEvents = 24;

class InputBatch final {
public:
    [[nodiscard]] bool push(const INPUT& input) noexcept {
        if (size_ >= inputs_.size()) return false;
        inputs_[size_++] = input;
        return true;
    }

    [[nodiscard]] INPUT* data() noexcept { return inputs_.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::array<INPUT, kMaximumInputEvents> inputs_{};
    std::size_t size_{};
};

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

bool appendClick(InputBatch& inputs, const valinvite::Point& point) {
    LONG x{};
    LONG y{};
    if (!screenPointToAbsolute(point, x, y)) return false;
    INPUT move{};
    move.type = INPUT_MOUSE;
    move.mi.dx = x;
    move.mi.dy = y;
    move.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE;
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return inputs.push(move) && inputs.push(down) && inputs.push(up);
}

bool appendVirtualKey(InputBatch& inputs, WORD key) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = key;
    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = key;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
    return inputs.push(down) && inputs.push(up);
}

bool appendUnicodeCharacter(InputBatch& inputs, char character) {
    const WORD value = static_cast<WORD>(static_cast<unsigned char>(character));
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = value;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wScan = value;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return inputs.push(down) && inputs.push(up);
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

    InputBatch inputs;
    if (!appendClick(inputs, config.inputPoint)) {
        error = L"无法转换输入框屏幕坐标";
        return false;
    }
    INPUT controlDown{};
    controlDown.type = INPUT_KEYBOARD;
    controlDown.ki.wVk = VK_CONTROL;
    if (!inputs.push(controlDown) || !appendVirtualKey(inputs, 'A')) {
        error = L"输入事件过多";
        return false;
    }
    INPUT controlUp{};
    controlUp.type = INPUT_KEYBOARD;
    controlUp.ki.wVk = VK_CONTROL;
    controlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    if (!inputs.push(controlUp)) {
        error = L"输入事件过多";
        return false;
    }
    for (const char character : code) {
        if (!appendUnicodeCharacter(inputs, character)) {
            error = L"输入事件过多";
            return false;
        }
    }
    if (config.submitMode == SubmitMode::Enter) {
        if (!appendVirtualKey(inputs, VK_RETURN)) {
            error = L"输入事件过多";
            return false;
        }
    } else {
        if (!appendClick(inputs, config.joinPoint)) {
            error = L"无法转换 Join 屏幕坐标";
            return false;
        }
    }

    const UINT count = static_cast<UINT>(inputs.size());
    const UINT sent = SendInput(count, inputs.data(), sizeof(INPUT));
    if (sent != count) {
        error = L"SendInput 未完整发送（" + std::to_wstring(sent) + L"/" + std::to_wstring(count)
            + L"，错误 " + std::to_wstring(GetLastError()) + L"）";
        return false;
    }
    return true;
}

} // namespace valinvite
