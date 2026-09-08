#pragma once

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
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

struct NormalizedRect final {
    double x{};
    double y{};
    double width{};
    double height{};

    [[nodiscard]] bool valid() const noexcept {
        constexpr double epsilon = 1e-6;
        return x >= 0.0 && y >= 0.0 && width > 0.0 && height > 0.0 &&
            x + width <= 1.0 + epsilon && y + height <= 1.0 + epsilon;
    }
};

struct Point final {
    int x{};
    int y{};
};

enum class SubmitMode { Enter, ClickJoin };
enum class RunState { Setup, Armed, Observing, Candidate, Confirmed, Stopped };

struct RecognitionConfig final {
    double scoreThreshold{0.79};
    double marginThreshold{0.08};
    double bboxTolerance{0.20};
    double backgroundThreshold{24.0};
};

struct Config final {
    std::wstring captureWindowTitle;
    NormalizedRect normalizedRoi;
    // Runtime pixel ROI resolved from normalizedRoi when START is pressed.
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
    char secondValue{};
    float bestScore{};
    float secondScore{};
    float margin{};
    int xStart{};
    int xEnd{};
    int bboxWidth{};
    int bboxHeight{};
};

struct Candidate final {
    std::array<MatchResult, 6> slots{};
    std::string code;
    bool structureValid{};
    bool boundingBoxesComplete{};
    bool edgeSlotsComplete{};
    bool backgroundProbeOk{};
    bool highConfidence{};
    // True when most slots carry real ink (each bbox above a small floor).
    // False when the ROI is blank/idle - the recognizer still hard-selects six
    // characters there, so structureValid alone cannot tell "no code" from
    // "code".
    bool codeVisible{};
};

struct TimingSnapshot final {
    double recognitionMs{};
    double decisionMs{};
    double dispatchMs{};
};

struct LatencyPercentiles final {
    double p50{};
    double p95{};
    double p99{};
    std::size_t samples{};
};

struct MemorySnapshot final {
    std::size_t workingSetBytes{};
    std::size_t privateBytes{};
    std::size_t peakWorkingSetBytes{};
};

} // namespace valinvite
