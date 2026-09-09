#include "recognizer.hpp"

#include "generated/glyph_templates.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace valinvite {
namespace {

constexpr int kCanvas = generated::kGlyphCanvas;
constexpr int kContent = generated::kGlyphContent;
constexpr std::size_t kPixels = static_cast<std::size_t>(kCanvas) * kCanvas;
constexpr int kRequiredGlyphs = 6;
constexpr int kProbeColumns = 2;
constexpr int kMinimumColumnPixels = 2;
constexpr int kMinimumRunWidth = 2;
constexpr int kMinimumRunPixels = 8;
constexpr int kMinimumGlyphHeight = 4;

static_assert(generated::kGlyphCount == 36);
static_assert(generated::kVariantsPerGlyph >= 3);
static_assert(kCanvas == 32);

struct Run final {
    int left{};
    int right{};
    int top{};
    int bottom{};
};

struct Features final {
    std::array<float, kPixels> centered{};
    std::array<float, kPixels> edges{};
    std::array<float, kCanvas> rowProjection{};
    std::array<float, kCanvas> columnProjection{};
    float norm{};
    float edgeNorm{};
    float rowNorm{};
    float columnNorm{};
};

[[nodiscard]] bool allowedInSlot(int slot, char value) noexcept {
    return slot < 3
        ? value >= 'A' && value <= 'Z'
        : value >= '0' && value <= '9';
}

[[nodiscard]] int templateIndex(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? value - 'A'
        : 26 + value - '0';
}

[[nodiscard]] bool ambiguityPair(char left, char right) noexcept {
    const auto matches = [left, right](char first, char second) noexcept {
        return (left == first && right == second) ||
            (left == second && right == first);
    };
    return matches('K', 'X') || matches('B', 'R') ||
        matches('D', 'B') || matches('D', 'O') ||
        matches('D', 'U') || matches('P', 'F');
}

[[nodiscard]] bool requiresStrictMargin(const MatchResult& slot) noexcept {
    if (ambiguityPair(slot.value, slot.secondValue)) {
        return true;
    }
    const bool wideM = slot.value == 'M' &&
        (slot.secondValue == 'N' || slot.secondValue == 'H') &&
        slot.bboxWidth > slot.bboxHeight;
    return wideM;
}

[[nodiscard]] bool looksLikeQTail(
    const std::array<std::uint8_t, kPixels>& glyph
) noexcept {
    int left = kCanvas;
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < kCanvas; ++y) {
        for (int x = 0; x < kCanvas; ++x) {
            if (glyph[static_cast<std::size_t>(y) * kCanvas + x] <= 32) continue;
            left = std::min(left, x);
            right = std::max(right, x);
            bottom = y;
        }
    }
    if (right < left || bottom < 0) return false;

    int tailLeft = kCanvas;
    int tailRight = -1;
    int tailPixels = 0;
    for (int x = 0; x < kCanvas; ++x) {
        if (glyph[static_cast<std::size_t>(bottom) * kCanvas + x] <= 32) continue;
        tailLeft = std::min(tailLeft, x);
        tailRight = std::max(tailRight, x);
        ++tailPixels;
    }

    const int width = right - left + 1;
    return tailRight >= tailLeft &&
        tailLeft >= left + (width * 3) / 5 &&
        tailPixels <= std::max(2, width / 3);
}

[[nodiscard]] int foregroundDelta(const RecognitionConfig& config) noexcept {
    return std::max(
        32,
        static_cast<int>(std::lround(config.backgroundThreshold + 8.0))
    );
}

[[nodiscard]] std::uint8_t estimateBackground(const GrayImageView& roi) noexcept {
    std::array<std::uint32_t, 256> histogram{};
    for (int y = 0; y < roi.height; ++y) {
        const auto* row = roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride;
        for (int x = 0; x < roi.width; ++x) {
            ++histogram[row[x]];
        }
    }

    const auto background = std::max_element(histogram.begin(), histogram.end());
    return static_cast<std::uint8_t>(std::distance(histogram.begin(), background));
}

[[nodiscard]] int pixelDelta(std::uint8_t value, std::uint8_t background) noexcept {
    return std::abs(static_cast<int>(value) - static_cast<int>(background));
}

[[nodiscard]] bool inspectRun(
    const GrayImageView& roi,
    int left,
    int right,
    std::uint8_t background,
    int threshold,
    Run& run
) noexcept {
    int top = roi.height;
    int bottom = -1;
    int pixels = 0;
    for (int y = 0; y < roi.height; ++y) {
        const auto* row = roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride;
        for (int x = left; x <= right; ++x) {
            if (pixelDelta(row[x], background) <= threshold) {
                continue;
            }
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            ++pixels;
        }
    }

    const int width = right - left + 1;
    const int height = bottom >= top ? bottom - top + 1 : 0;
    if (width < kMinimumRunWidth || height < kMinimumGlyphHeight ||
        pixels < kMinimumRunPixels) {
        return false;
    }
    run = {left, right, top, bottom};
    return true;
}

[[nodiscard]] int segmentRuns(
    const GrayImageView& roi,
    std::uint8_t background,
    int threshold,
    std::array<Run, kRequiredGlyphs>& output
) noexcept {
    int outputCount = 0;
    int start = -1;

    const auto finish = [&](int right) noexcept -> bool {
        if (start < 0) {
            return true;
        }
        Run run{};
        if (inspectRun(roi, start, right, background, threshold, run)) {
            if (outputCount >= kRequiredGlyphs) {
                return false;
            }
            output[outputCount++] = run;
        }
        return true;
    };

    for (int x = 0; x < roi.width; ++x) {
        int foregroundPixels = 0;
        for (int y = 0; y < roi.height; ++y) {
            const auto value = roi.pixels[
                static_cast<std::ptrdiff_t>(y) * roi.stride + x
            ];
            if (pixelDelta(value, background) > threshold) {
                ++foregroundPixels;
            }
        }

        const bool active = foregroundPixels >= kMinimumColumnPixels;
        if (active && start < 0) {
            start = x;
        }
        else if (!active && start >= 0) {
            if (!finish(x - 1)) {
                return outputCount + 1;
            }
            start = -1;
        }
    }
    if (start >= 0 && !finish(roi.width - 1)) {
        return outputCount + 1;
    }
    return outputCount;
}

void normalizeGlyph(
    const GrayImageView& roi,
    const Run& run,
    std::uint8_t background,
    int threshold,
    std::array<std::uint8_t, kPixels>& output
) noexcept {
    output.fill(0);
    const int sourceWidth = run.right - run.left + 1;
    const int sourceHeight = run.bottom - run.top + 1;
    const float scale = std::min(
        static_cast<float>(kContent) / static_cast<float>(sourceWidth),
        static_cast<float>(kContent) / static_cast<float>(sourceHeight)
    );
    const int targetWidth = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(sourceWidth) * scale))
    );
    const int targetHeight = std::max(
        1,
        static_cast<int>(std::lround(static_cast<float>(sourceHeight) * scale))
    );
    const int targetLeft = (kCanvas - targetWidth) / 2;
    const int targetTop = (kCanvas - targetHeight) / 2;

    const auto sample = [&](int x, int y) noexcept -> float {
        const auto value = roi.pixels[
            static_cast<std::ptrdiff_t>(run.top + y) * roi.stride + run.left + x
        ];
        const int delta = pixelDelta(value, background);
        return delta > threshold ? static_cast<float>(delta) : 0.0F;
    };

    for (int y = 0; y < targetHeight; ++y) {
        const float sourceY =
            (static_cast<float>(y) + 0.5F) * static_cast<float>(sourceHeight) /
                static_cast<float>(targetHeight) - 0.5F;
        const int y0 = std::clamp(
            static_cast<int>(std::floor(sourceY)), 0, sourceHeight - 1
        );
        const int y1 = std::min(sourceHeight - 1, y0 + 1);
        const float fy = std::clamp(sourceY - static_cast<float>(y0), 0.0F, 1.0F);

        for (int x = 0; x < targetWidth; ++x) {
            const float sourceX =
                (static_cast<float>(x) + 0.5F) * static_cast<float>(sourceWidth) /
                    static_cast<float>(targetWidth) - 0.5F;
            const int x0 = std::clamp(
                static_cast<int>(std::floor(sourceX)), 0, sourceWidth - 1
            );
            const int x1 = std::min(sourceWidth - 1, x0 + 1);
            const float fx = std::clamp(sourceX - static_cast<float>(x0), 0.0F, 1.0F);
            const float top = sample(x0, y0) * (1.0F - fx) + sample(x1, y0) * fx;
            const float bottom = sample(x0, y1) * (1.0F - fx) + sample(x1, y1) * fx;
            const float value = top * (1.0F - fy) + bottom * fy;
            output[static_cast<std::size_t>(targetTop + y) * kCanvas + targetLeft + x] =
                static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
        }
    }
}

[[nodiscard]] Features makeFeatures(const std::uint8_t* pixels) noexcept {
    Features features{};
    float mean{};
    for (std::size_t i = 0; i < kPixels; ++i) {
        mean += static_cast<float>(pixels[i]);
    }
    mean /= static_cast<float>(kPixels);

    float normSquared{};
    float edgeNormSquared{};
    for (int y = 0; y < kCanvas; ++y) {
        for (int x = 0; x < kCanvas; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * kCanvas + x;
            const float centered = static_cast<float>(pixels[index]) - mean;
            features.centered[index] = centered;
            normSquared += centered * centered;
            features.rowProjection[y] += static_cast<float>(pixels[index]);
            features.columnProjection[x] += static_cast<float>(pixels[index]);

            const int left = std::max(0, x - 1);
            const int right = std::min(kCanvas - 1, x + 1);
            const int top = std::max(0, y - 1);
            const int bottom = std::min(kCanvas - 1, y + 1);
            const float dx = static_cast<float>(
                pixels[static_cast<std::size_t>(y) * kCanvas + right]
            ) - static_cast<float>(
                pixels[static_cast<std::size_t>(y) * kCanvas + left]
            );
            const float dy = static_cast<float>(
                pixels[static_cast<std::size_t>(bottom) * kCanvas + x]
            ) - static_cast<float>(
                pixels[static_cast<std::size_t>(top) * kCanvas + x]
            );
            const float edge = std::hypot(dx, dy);
            features.edges[index] = edge;
            edgeNormSquared += edge * edge;
        }
    }
    features.norm = std::sqrt(normSquared);
    features.edgeNorm = std::sqrt(edgeNormSquared);
    float rowNormSquared{};
    float columnNormSquared{};
    for (int index = 0; index < kCanvas; ++index) {
        rowNormSquared += features.rowProjection[index] * features.rowProjection[index];
        columnNormSquared +=
            features.columnProjection[index] * features.columnProjection[index];
    }
    features.rowNorm = std::sqrt(rowNormSquared);
    features.columnNorm = std::sqrt(columnNormSquared);
    return features;
}

[[nodiscard]] float similarity(
    const std::array<std::uint8_t, kPixels>& candidate,
    const Features& candidateFeatures,
    const std::uint8_t* templ,
    const Features& templateFeatures
) noexcept {
    std::uint64_t difference{};
    float correlationDot{};
    float edgeDot{};
    float rowDot{};
    float columnDot{};
    std::uint32_t intersection{};
    std::uint32_t unionPixels{};
    for (std::size_t i = 0; i < kPixels; ++i) {
        difference += static_cast<unsigned>(std::abs(
            static_cast<int>(candidate[i]) - static_cast<int>(templ[i])
        ));
        correlationDot += candidateFeatures.centered[i] * templateFeatures.centered[i];
        edgeDot += candidateFeatures.edges[i] * templateFeatures.edges[i];
        const bool candidateInk = candidate[i] > 32;
        const bool templateInk = templ[i] > 32;
        intersection += candidateInk && templateInk ? 1U : 0U;
        unionPixels += candidateInk || templateInk ? 1U : 0U;
    }
    for (int index = 0; index < kCanvas; ++index) {
        rowDot += candidateFeatures.rowProjection[index] *
            templateFeatures.rowProjection[index];
        columnDot += candidateFeatures.columnProjection[index] *
            templateFeatures.columnProjection[index];
    }

    const float sad = 1.0F - static_cast<float>(difference) /
        static_cast<float>(255ULL * kPixels);
    const float correlationDenominator = candidateFeatures.norm * templateFeatures.norm;
    const float edgeDenominator = candidateFeatures.edgeNorm * templateFeatures.edgeNorm;
    const float correlation = correlationDenominator > 0.0F
        ? std::max(0.0F, correlationDot / correlationDenominator)
        : 0.0F;
    const float edge = edgeDenominator > 0.0F
        ? std::max(0.0F, edgeDot / edgeDenominator)
        : 0.0F;
    const float overlap = unionPixels > 0
        ? static_cast<float>(intersection) / static_cast<float>(unionPixels)
        : 0.0F;
    const float rowDenominator = candidateFeatures.rowNorm * templateFeatures.rowNorm;
    const float columnDenominator =
        candidateFeatures.columnNorm * templateFeatures.columnNorm;
    const float projection = 0.5F * (
        (rowDenominator > 0.0F ? rowDot / rowDenominator : 0.0F) +
        (columnDenominator > 0.0F ? columnDot / columnDenominator : 0.0F)
    );
    return std::clamp(
        0.15F * sad + 0.30F * correlation + 0.15F * edge +
            0.25F * overlap + 0.15F * projection,
        0.0F,
        1.0F
    );
}

[[nodiscard]] bool backgroundProbe(
    const GrayImageView& roi,
    std::uint8_t background,
    double threshold
) noexcept {
    std::uint64_t difference{};
    std::uint64_t samples{};
    const int columns = std::min(kProbeColumns, roi.width);
    for (int y = 0; y < roi.height; ++y) {
        const auto* row = roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride;
        for (int x = 0; x < columns; ++x) {
            difference += static_cast<unsigned>(pixelDelta(row[x], background));
            ++samples;
        }
        for (int x = std::max(columns, roi.width - columns); x < roi.width; ++x) {
            difference += static_cast<unsigned>(pixelDelta(row[x], background));
            ++samples;
        }
    }
    return samples > 0 &&
        static_cast<double>(difference) / static_cast<double>(samples) <= threshold;
}

} // namespace

struct Recognizer::Impl final {
    std::array<
        std::array<Features, generated::kVariantsPerGlyph>,
        generated::kGlyphCount
    > features{};

    Impl() noexcept {
        for (std::size_t glyph = 0; glyph < generated::kGlyphCount; ++glyph) {
            for (std::size_t variant = 0; variant < generated::kVariantsPerGlyph; ++variant) {
                features[glyph][variant] = makeFeatures(
                    generated::kGlyphTemplates[glyph][variant]
                );
            }
        }
    }
};

Recognizer::Recognizer()
    : impl_{std::make_unique<Impl>()} {
}

Recognizer::~Recognizer() = default;

void Recognizer::setConfig(const RecognitionConfig& config) noexcept {
    config_ = config;
}

bool Recognizer::ready() const noexcept {
    return impl_ != nullptr;
}

Candidate Recognizer::recognize(const GrayImageView& roi) const {
    Candidate candidate{};
    if (!roi.valid() || !ready() || roi.width < kRequiredGlyphs || roi.height < 1) {
        return candidate;
    }

    const std::uint8_t background = estimateBackground(roi);
    const int threshold = foregroundDelta(config_);
    std::array<Run, kRequiredGlyphs> runs{};
    if (segmentRuns(roi, background, threshold, runs) != kRequiredGlyphs) {
        return candidate;
    }

    bool allScores = true;
    std::array<std::uint8_t, kPixels> normalized{};
    for (int slot = 0; slot < kRequiredGlyphs; ++slot) {
        normalizeGlyph(roi, runs[slot], background, threshold, normalized);
        const Features candidateFeatures = makeFeatures(normalized.data());
        const bool qTail = slot < 3 && looksLikeQTail(normalized);
        float bestScore = -std::numeric_limits<float>::infinity();
        float secondScore = -std::numeric_limits<float>::infinity();
        char bestValue{};
        char secondValue{};

        for (const char value : generated::kGlyphValues) {
            if (value == '\0' || !allowedInSlot(slot, value)) {
                continue;
            }
            const int glyph = templateIndex(value);
            float characterBest = -std::numeric_limits<float>::infinity();
            for (std::size_t variant = 0; variant < generated::kVariantsPerGlyph; ++variant) {
                characterBest = std::max(
                    characterBest,
                    similarity(
                        normalized,
                        candidateFeatures,
                        generated::kGlyphTemplates[glyph][variant],
                        impl_->features[glyph][variant]
                    )
                );
            }
            // Browser rasterization leaves Q's short lower-right tail thinner
            // than the font atlas. Preserve that structural evidence so Q is
            // not flattened into O on real captured frames.
            if (value == 'Q' && qTail) {
                characterBest = std::min(1.0F, characterBest + 0.10F);
            }
            if (characterBest > bestScore) {
                secondScore = bestScore;
                secondValue = bestValue;
                bestScore = characterBest;
                bestValue = value;
            }
            else if (characterBest > secondScore) {
                secondScore = characterBest;
                secondValue = value;
            }
        }

        const float safeSecond = std::max(0.0F, secondScore);
        const Run& run = runs[slot];
        candidate.slots[slot] = {
            bestValue,
            secondValue,
            bestScore,
            safeSecond,
            bestScore - safeSecond,
            run.left,
            run.right,
            run.right - run.left + 1,
            run.bottom - run.top + 1
        };
        candidate.code.push_back(bestValue);
        allScores = allScores &&
            bestScore >= config_.scoreThreshold &&
            candidate.slots[slot].margin >= config_.marginThreshold;
    }

    candidate.structureValid = candidate.code.size() == kRequiredGlyphs;
    candidate.boundingBoxesComplete = true;
    candidate.edgeSlotsComplete = true;
    candidate.backgroundProbeOk = backgroundProbe(
        roi,
        background,
        config_.backgroundThreshold
    );
    candidate.codeVisible = true;
    candidate.highConfidence = candidate.structureValid && allScores &&
        candidate.backgroundProbeOk;
    return candidate;
}

Candidate Recognizer::evaluate(std::string_view code) const {
    Candidate candidate{};
    candidate.code = code;
    candidate.structureValid = code.size() == 6 &&
        std::all_of(code.begin(), code.begin() + 3, [](char value) {
            return value >= 'A' && value <= 'Z';
        }) &&
        std::all_of(code.begin() + 3, code.end(), [](char value) {
            return value >= '0' && value <= '9';
        });
    return candidate;
}

bool Recognizer::shouldSubmit(
    const Candidate& candidate,
    const std::optional<std::string>& previous
) const {
    if (!candidate.structureValid || !candidate.boundingBoxesComplete ||
        !candidate.edgeSlotsComplete || !candidate.backgroundProbeOk ||
        !candidate.codeVisible) {
        return false;
    }
    if (candidate.highConfidence) {
        return true;
    }
    if (!previous || *previous != candidate.code) {
        return false;
    }

    const double fallbackScore = std::max(0.0, config_.scoreThreshold - 0.03);
    const double fallbackMargin = std::max(0.0, config_.marginThreshold * 0.125);
    return std::all_of(
        candidate.slots.begin(),
        candidate.slots.end(),
        [this, fallbackScore, fallbackMargin](const MatchResult& slot) {
            const double requiredMargin = requiresStrictMargin(slot)
                ? std::max(fallbackMargin, config_.marginThreshold)
                : fallbackMargin;
            return static_cast<double>(slot.bestScore) >= fallbackScore &&
                static_cast<double>(slot.margin) >= requiredMargin;
        }
    );
}

} // namespace valinvite
