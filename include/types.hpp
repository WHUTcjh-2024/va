#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace valinvite {

struct Rect final {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

struct Point final {
    int x{};
    int y{};
};

enum class SubmitMode { Enter, ClickJoin };
enum class RunState { Setup, Armed, Observing, Candidate, Confirmed, Stopped };

struct RecognitionConfig final {
    double scoreThreshold{0.85};
    double marginThreshold{0.08};
    double bboxTolerance{0.20};
    double backgroundThreshold{24.0};
};

struct Config final {
    std::wstring captureWindowTitle;
    Rect roi;
    RecognitionConfig recognition;
    Point inputPoint;
    Point joinPoint;
    SubmitMode submitMode{SubmitMode::Enter};
    bool highPriority{true};
    int cpuAffinity{-1};
};

struct MatchResult final {
    char value{};
    float bestScore{};
    float secondScore{};
    float margin{};
    int bboxWidth{};
    int bboxHeight{};
};

struct Candidate final {
    std::array<MatchResult, 6> slots{};
    std::string code;
    bool structureValid{};
    bool boundingBoxesComplete{};
    bool backgroundProbeOk{};
    bool highConfidence{};
};

struct TimingSnapshot final {
    double recognitionMs{};
    double decisionMs{};
    double dispatchMs{};
};

} // namespace valinvite
