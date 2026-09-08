#include "recognizer.hpp"

#include "generated/glyph_templates.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kCanvas = valinvite::generated::kGlyphCanvas;
constexpr int kBaseGlyphHeight = 28;
constexpr int kPadding = 4;
constexpr int kGap = 2;

struct GlyphBitmap final {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
};

struct RoiFrame final {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
};

struct Counts final {
    int correct{};
    int wrong{};
    int rejected{};
    int top1Wrong{};
    float highestWrongMinScore{};
    float highestWrongMinMargin{};
    std::array<int, 36 * 36> confusions{};
};

struct Distortion final {
    bool blur{};
    int brightnessAmplitude{};
    int noiseAmplitude{};
    bool horizontalJitter{};
};

[[nodiscard]] int glyphIndex(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? value - 'A'
        : 26 + value - '0';
}

[[nodiscard]] std::uint8_t bilinear(
    const std::uint8_t* source,
    int sourceStride,
    int sourceWidth,
    int sourceHeight,
    int x,
    int y,
    int targetWidth,
    int targetHeight
) noexcept {
    const float sourceX =
        (static_cast<float>(x) + 0.5F) * sourceWidth / targetWidth - 0.5F;
    const float sourceY =
        (static_cast<float>(y) + 0.5F) * sourceHeight / targetHeight - 0.5F;
    const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, sourceWidth - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, sourceHeight - 1);
    const int x1 = std::min(sourceWidth - 1, x0 + 1);
    const int y1 = std::min(sourceHeight - 1, y0 + 1);
    const float fx = std::clamp(sourceX - static_cast<float>(x0), 0.0F, 1.0F);
    const float fy = std::clamp(sourceY - static_cast<float>(y0), 0.0F, 1.0F);
    const float top = source[y0 * sourceStride + x0] * (1.0F - fx) +
        source[y0 * sourceStride + x1] * fx;
    const float bottom = source[y1 * sourceStride + x0] * (1.0F - fx) +
        source[y1 * sourceStride + x1] * fx;
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(top * (1.0F - fy) + bottom * fy),
        0L,
        255L
    ));
}

[[nodiscard]] GlyphBitmap makeGlyph(
    char value,
    std::size_t variant,
    float scale,
    float horizontalScale
) {
    const auto* source = valinvite::generated::kGlyphTemplates[glyphIndex(value)][variant];
    int minX = kCanvas;
    int minY = kCanvas;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < kCanvas; ++y) {
        for (int x = 0; x < kCanvas; ++x) {
            if (source[y * kCanvas + x] <= 32) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    const int sourceWidth = maxX - minX + 1;
    const int sourceHeight = maxY - minY + 1;
    const int targetHeight = std::max(4, static_cast<int>(std::lround(kBaseGlyphHeight * scale)));
    const int targetWidth = std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<float>(sourceWidth) * targetHeight / sourceHeight * horizontalScale
        ))
    );
    GlyphBitmap glyph{
        targetWidth,
        targetHeight,
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(targetWidth) * targetHeight
        )
    };
    for (int y = 0; y < targetHeight; ++y) {
        for (int x = 0; x < targetWidth; ++x) {
            glyph.pixels[static_cast<std::size_t>(y) * targetWidth + x] = bilinear(
                source + minY * kCanvas + minX,
                kCanvas,
                sourceWidth,
                sourceHeight,
                x,
                y,
                targetWidth,
                targetHeight
            );
        }
    }
    return glyph;
}

void blur(RoiFrame& frame) {
    std::vector<std::uint8_t> output = frame.pixels;
    for (int y = 1; y + 1 < frame.height; ++y) {
        for (int x = 1; x + 1 < frame.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * frame.width + x;
            const int sum = 12 * frame.pixels[index] +
                frame.pixels[index - 1] + frame.pixels[index + 1] +
                frame.pixels[index - frame.width] + frame.pixels[index + frame.width];
            output[static_cast<std::size_t>(y) * frame.width + x] =
                static_cast<std::uint8_t>((sum + 8) / 16);
        }
    }
    frame.pixels = std::move(output);
}

[[nodiscard]] RoiFrame compose(
    std::string_view code,
    float scale,
    int offset,
    bool applyBlur,
    int brightnessDelta,
    int noiseAmplitude,
    bool horizontalJitter,
    std::mt19937& random
) {
    std::uniform_int_distribution<int> variant(
        0,
        static_cast<int>(valinvite::generated::kVariantsPerGlyph - 1)
    );
    std::uniform_real_distribution<float> horizontal(0.93F, 1.07F);
    std::vector<GlyphBitmap> glyphs;
    glyphs.reserve(code.size());
    int contentWidth{};
    int contentHeight{};
    for (const char value : code) {
        glyphs.push_back(makeGlyph(
            value,
            static_cast<std::size_t>(variant(random)),
            scale,
            horizontalJitter ? horizontal(random) : 1.0F
        ));
        contentWidth += glyphs.back().width;
        contentHeight = std::max(contentHeight, glyphs.back().height);
    }
    if (!glyphs.empty()) {
        contentWidth += kGap * (static_cast<int>(glyphs.size()) - 1);
    }

    const int background = 45;
    RoiFrame frame{
        contentWidth + kPadding * 2,
        contentHeight + kPadding * 2,
        std::vector<std::uint8_t>(
            static_cast<std::size_t>(contentWidth + kPadding * 2) *
                (contentHeight + kPadding * 2),
            static_cast<std::uint8_t>(background)
        )
    };

    int cursor = kPadding + offset;
    for (const GlyphBitmap& glyph : glyphs) {
        const int top = kPadding + (contentHeight - glyph.height) / 2 + offset;
        for (int y = 0; y < glyph.height; ++y) {
            for (int x = 0; x < glyph.width; ++x) {
                const int targetX = cursor + x;
                const int targetY = top + y;
                if (targetX < 0 || targetX >= frame.width ||
                    targetY < 0 || targetY >= frame.height) {
                    continue;
                }
                const int ink = glyph.pixels[
                    static_cast<std::size_t>(y) * glyph.width + x
                ];
                const int value = background + brightnessDelta + ink;
                frame.pixels[static_cast<std::size_t>(targetY) * frame.width + targetX] =
                    static_cast<std::uint8_t>(std::clamp(value, 0, 255));
            }
        }
        cursor += glyph.width + kGap;
    }

    if (applyBlur) {
        blur(frame);
    }
    if (noiseAmplitude > 0) {
        std::uniform_int_distribution<int> noise(-noiseAmplitude, noiseAmplitude);
        for (auto& value : frame.pixels) {
            value = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(value) + noise(random),
                0,
                255
            ));
        }
    }
    return frame;
}

[[nodiscard]] std::string randomCode(std::mt19937& random) {
    std::uniform_int_distribution<int> letter(0, 25);
    std::uniform_int_distribution<int> digit(0, 9);
    std::string code;
    code.reserve(6);
    for (int i = 0; i < 3; ++i) {
        code.push_back(static_cast<char>('A' + letter(random)));
    }
    for (int i = 0; i < 3; ++i) {
        code.push_back(static_cast<char>('0' + digit(random)));
    }
    return code;
}

[[nodiscard]] Counts runScenario(
    valinvite::Recognizer& recognizer,
    int trials,
    float scale,
    int offset,
    Distortion distortion,
    std::mt19937& random,
    std::vector<double>* timings
) {
    Counts counts{};
    std::uniform_int_distribution<int> brightness(
        -distortion.brightnessAmplitude,
        distortion.brightnessAmplitude
    );
    std::uniform_int_distribution<int> noise(
        0,
        distortion.noiseAmplitude
    );
    for (int trial = 0; trial < trials; ++trial) {
        const std::string expected = randomCode(random);
        RoiFrame frame = compose(
            expected,
            scale,
            offset,
            distortion.blur,
            brightness(random),
            noise(random),
            distortion.horizontalJitter,
            random
        );
        const auto start = std::chrono::steady_clock::now();
        const valinvite::Candidate candidate = recognizer.recognize({
            frame.pixels.data(), frame.width, frame.height, frame.width
        });
        const auto end = std::chrono::steady_clock::now();
        if (timings != nullptr) {
            timings->push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        if (candidate.code != expected) {
            ++counts.top1Wrong;
            if (counts.top1Wrong <= 10) {
                std::printf("top1 sample       expected=%s recognized=%s", expected.c_str(), candidate.code.c_str());
                for (std::size_t slot = 0; slot < candidate.slots.size(); ++slot) {
                    const auto& match = candidate.slots[slot];
                    if (slot < expected.size() && match.value != expected[slot]) {
                        std::printf(
                            " slot%zu=%c/%c score=%.4f margin=%.4f bbox=%dx%d",
                            slot, match.value, match.secondValue, match.bestScore,
                            match.margin, match.bboxWidth, match.bboxHeight
                        );
                    }
                }
                std::printf("\n");
            }
            if (candidate.code.size() == expected.size()) {
                for (std::size_t slot = 0; slot < expected.size(); ++slot) {
                    if (candidate.code[slot] != expected[slot]) {
                        ++counts.confusions[
                            static_cast<std::size_t>(glyphIndex(expected[slot])) * 36 +
                            glyphIndex(candidate.code[slot])
                        ];
                    }
                }
            }
        }
        const bool submit = recognizer.shouldSubmit(candidate, candidate.code);
        if (!submit) {
            ++counts.rejected;
        }
        else if (candidate.code == expected) {
            ++counts.correct;
        }
        else {
            ++counts.wrong;
            float minimumScore = 1.0F;
            float minimumMargin = 1.0F;
            for (const auto& slot : candidate.slots) {
                minimumScore = std::min(minimumScore, slot.bestScore);
                minimumMargin = std::min(minimumMargin, slot.margin);
            }
            counts.highestWrongMinScore = std::max(
                counts.highestWrongMinScore,
                minimumScore
            );
            counts.highestWrongMinMargin = std::max(
                counts.highestWrongMinMargin,
                minimumMargin
            );
        }
    }
    return counts;
}

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    return values[lower] + (values[upper] - values[lower]) *
        (position - std::floor(position));
}

void printCounts(std::string_view label, const Counts& counts) {
    std::printf(
        "%-18.*s Correct=%d Wrong=%d Rejected=%d Top1Wrong=%d "
        "WrongMaxMinScore=%.4f WrongMaxMinMargin=%.4f\n",
        static_cast<int>(label.size()),
        label.data(),
        counts.correct,
        counts.wrong,
        counts.rejected,
        counts.top1Wrong,
        counts.highestWrongMinScore,
        counts.highestWrongMinMargin
    );
}

void printConfusions(const Counts& counts) {
    auto confusions = counts.confusions;
    std::printf(
        "wrong confidence   max(minScore)=%.4f max(minMargin)=%.4f\n",
        counts.highestWrongMinScore,
        counts.highestWrongMinMargin
    );
    std::printf("top confusions    ");
    for (int rank = 0; rank < 10; ++rank) {
        const auto found = std::max_element(confusions.begin(), confusions.end());
        if (found == confusions.end() || *found == 0) {
            break;
        }
        const int index = static_cast<int>(std::distance(confusions.begin(), found));
        const int expected = index / 36;
        const int recognized = index % 36;
        std::printf(
            "%c->%c:%d ",
            valinvite::generated::kGlyphValues[expected],
            valinvite::generated::kGlyphValues[recognized],
            *found
        );
        *found = 0;
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    const int trials = argc > 1 ? std::max(1, std::atoi(argv[1])) : 10000;
    valinvite::RecognitionConfig config{};
    if (argc > 2) {
        config.scoreThreshold = std::atof(argv[2]);
    }
    if (argc > 3) {
        config.marginThreshold = std::atof(argv[3]);
    }

    valinvite::Recognizer recognizer;
    recognizer.setConfig(config);
    std::mt19937 random(0x5a17U);
    std::vector<double> timings;
    timings.reserve(static_cast<std::size_t>(trials));

    std::printf(
        "thresholds score=%.4f margin=%.4f templates=%zu\n",
        config.scoreThreshold,
        config.marginThreshold,
        valinvite::generated::kGlyphCount * valinvite::generated::kVariantsPerGlyph
    );
    const Counts unseen = runScenario(
        recognizer, trials, 1.0F, 0, {}, random, &timings
    );
    printCounts("unseen", unseen);
    printConfusions(unseen);

    for (const float scale : {0.60F, 0.75F, 1.00F, 1.25F, 1.50F}) {
        const Counts result = runScenario(
            recognizer, 500, scale, 0, {}, random, nullptr
        );
        char label[32]{};
        std::snprintf(label, sizeof(label), "scale %.2f", static_cast<double>(scale));
        printCounts(label, result);
        if (result.top1Wrong > 0) {
            printConfusions(result);
        }
    }
    for (const int offset : {-2, -1, 1, 2}) {
        const Counts result = runScenario(
            recognizer, 500, 1.0F, offset, {}, random, nullptr
        );
        char label[32]{};
        std::snprintf(label, sizeof(label), "ROI offset %+d", offset);
        printCounts(label, result);
        if (result.top1Wrong > 0) {
            printConfusions(result);
        }
    }

    const std::array<std::pair<std::string_view, Distortion>, 3> distortions{{
        {"brightness", {false, 12, 0, false}},
        {"light blur", {true, 0, 0, false}},
        {"AA / jitter", {false, 0, 2, true}},
    }};
    for (const auto& [label, distortion] : distortions) {
        const Counts result = runScenario(
            recognizer, 500, 1.0F, 0, distortion, random, nullptr
        );
        printCounts(label, result);
        if (result.top1Wrong > 0) {
            printConfusions(result);
        }
    }

    int early = 0;
    for (int visible = 1; visible < 6; ++visible) {
        const std::string code = randomCode(random);
        RoiFrame frame = compose(
            std::string_view{code}.substr(0, static_cast<std::size_t>(visible)),
            1.0F,
            0,
            false,
            0,
            0,
            false,
            random
        );
        const auto candidate = recognizer.recognize({
            frame.pixels.data(), frame.width, frame.height, frame.width
        });
        if (recognizer.shouldSubmit(candidate, candidate.code)) {
            ++early;
        }
    }
    std::printf("partial 1..5      Early=%d\n", early);
    std::printf(
        "Recognition ms     P50=%.3f P95=%.3f P99=%.3f\n",
        percentile(timings, 0.50),
        percentile(timings, 0.95),
        percentile(timings, 0.99)
    );

    return unseen.wrong == 0 && early == 0 ? 0 : 1;
}
