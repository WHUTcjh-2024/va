// Offline test. Build manually from repository root:
// cl /std:c++20 /EHsc /I include templates\static_sample_test.cpp src\recognizer.cpp src\timer.cpp
#include "recognizer.hpp"
#include "timer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr int kTemplateSide = 32;

std::vector<std::uint8_t> loadTemplate(char value) {
    std::ifstream file("templates/" + std::string(1, value) + "/01.bin");
    std::string line;
    std::getline(file, line);
    std::getline(file, line);
    std::vector<std::uint8_t> result;
    char marker{};
    while (file.get(marker) && result.size() < kTemplateSide * kTemplateSide) {
        if (marker == '.') result.push_back(20);
        if (marker == '#') result.push_back(235);
    }
    return result;
}

std::vector<std::uint8_t> compose(std::string_view code, int slotWidth) {
    std::vector<std::uint8_t> roi(static_cast<std::size_t>(slotWidth * 6 * kTemplateSide), 20);
    for (int slot = 0; slot < 6; ++slot) {
        const auto templ = loadTemplate(code[slot]);
        if (templ.size() != kTemplateSide * kTemplateSide) return {};
        for (int y = 0; y < kTemplateSide; ++y) {
            for (int x = 0; x < kTemplateSide; ++x) {
                const int scaledX = (x * slotWidth) / kTemplateSide;
                roi[static_cast<std::size_t>(y) * slotWidth * 6 + slot * slotWidth + scaledX] = templ[static_cast<std::size_t>(y) * kTemplateSide + x];
            }
        }
    }
    return roi;
}

bool matches(valinvite::Recognizer& recognizer, std::string_view expected, int slotWidth) {
    const auto pixels = compose(expected, slotWidth);
    const auto candidate = recognizer.recognize({pixels.data(), slotWidth * 6, kTemplateSide, slotWidth * 6});
    return candidate.code == expected && candidate.structureValid && candidate.boundingBoxesComplete && candidate.edgeSlotsComplete && candidate.backgroundProbeOk && candidate.highConfidence;
}
} // namespace

int main() {
    valinvite::Recognizer recognizer;
    std::wstring error;
    if (!recognizer.loadTemplates("templates", error)) return 10;
    valinvite::RecognitionConfig config;
    config.marginThreshold = 0.01;
    recognizer.setConfig(config);

    for (const std::string_view code : {"AAA999", "III111", "WWW888", "LLL777", "MMM333"}) {
        if (!matches(recognizer, code, 32) || !matches(recognizer, code, 33)) return 20;
    }
    if (recognizer.shouldSubmit(recognizer.evaluate("AAA999"), std::nullopt)) return 21;

    auto partialPixels = compose("AAA999", 32);
    for (int y = 0; y < kTemplateSide; ++y) std::fill_n(partialPixels.begin() + static_cast<std::size_t>(y) * 192, 32, static_cast<std::uint8_t>(20));
    const auto partial = recognizer.recognize({partialPixels.data(), 192, 32, 192});
    if (partial.boundingBoxesComplete || partial.edgeSlotsComplete || recognizer.shouldSubmit(partial, std::nullopt)) return 30;

    auto probePixels = compose("AAA999", 32);
    for (int y = 0; y < kTemplateSide; ++y) probePixels[static_cast<std::size_t>(y) * 192] = 235;
    const auto probeFailure = recognizer.recognize({probePixels.data(), 192, 32, 192});
    if (probeFailure.backgroundProbeOk || recognizer.shouldSubmit(probeFailure, std::nullopt)) return 31;

    valinvite::HighResolutionTimer timer;
    timer.reserveSamples(5);
    for (const std::string_view metric : {"recognition", "decision", "frame_to_input_dispatch"}) {
        for (const double sample : {1.0, 2.0, 3.0, 4.0, 5.0}) timer.record(metric, sample);
        const auto stats = timer.percentiles(metric);
        if (stats.samples != 5 || stats.p50 != 3.0 || stats.p95 != 4.8 || stats.p99 != 4.96) return 40;
    }
    if (timer.memory().workingSetBytes == 0) return 50;
    std::cout << "offline recognizer gates and timer statistics passed\n";
}
