#include "recognizer.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

#include <intrin.h>

#if defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace valinvite {
namespace {

constexpr std::array<char, 36> kAlphabet{
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '0','1','2','3','4','5','6','7','8','9'
};

constexpr std::array<char, 4> kTemplateMagic{'V', 'I', 'T', '1'};

constexpr int kTemplateWidth = 32;
constexpr int kTemplateHeight = 32;
constexpr std::size_t kTemplatePixels =
    static_cast<std::size_t>(kTemplateWidth) * kTemplateHeight;

constexpr int kProbeColumns = 2;

[[nodiscard]] bool allowedInSlot(int slot, char value) noexcept {
    return slot < 3
        ? value >= 'A' && value <= 'Z'
        : value >= '0' && value <= '9';
}

[[nodiscard]] std::uint16_t readU16(std::istream& stream) {
    std::array<unsigned char, 2> bytes{};
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    return static_cast<std::uint16_t>(
        bytes[0] |
        (static_cast<std::uint16_t>(bytes[1]) << 8)
    );
}

[[nodiscard]] std::uint64_t sadScalar(
    const std::uint8_t* image,
    const std::uint8_t* templ,
    std::size_t count
) noexcept {
    std::uint64_t total{};

    for (std::size_t i = 0; i < count; ++i) {
        total += static_cast<unsigned>(
            std::abs(
                static_cast<int>(image[i]) -
                static_cast<int>(templ[i])
            )
        );
    }

    return total;
}

#if defined(_M_X64) || defined(_M_IX86)

[[nodiscard]] std::uint64_t sadAvx2(
    const std::uint8_t* image,
    const std::uint8_t* templ,
    std::size_t count
) noexcept {
    std::size_t i{};
    __m256i accumulator = _mm256_setzero_si256();

    for (; i + 32 <= count; i += 32) {
        const __m256i a = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(image + i)
        );

        const __m256i b = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(templ + i)
        );

        const __m256i sad = _mm256_sad_epu8(a, b);

        accumulator = _mm256_add_epi64(
            accumulator,
            sad
        );
    }

    alignas(32) std::array<std::uint64_t, 4> lanes{};

    _mm256_store_si256(
        reinterpret_cast<__m256i*>(lanes.data()),
        accumulator
    );

    std::uint64_t total =
        lanes[0] + lanes[1] + lanes[2] + lanes[3];

    if (i < count) {
        total += sadScalar(
            image + i,
            templ + i,
            count - i
        );
    }

    return total;
}

#endif

[[nodiscard]] bool cpuHasAvx2() noexcept {
#if defined(_M_X64) || defined(_M_IX86)
    int info[4]{};

    __cpuidex(info, 0, 0);

    if (info[0] < 7) {
        return false;
    }

    __cpuidex(info, 1, 0);

    if ((info[2] & (1 << 27)) == 0 ||
        (info[2] & (1 << 28)) == 0) {
        return false;
    }

    if ((_xgetbv(0) & 0x6U) != 0x6U) {
        return false;
    }

    __cpuidex(info, 7, 0);

    return (info[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}

void normalizeSlot(
    const GrayImageView& roi,
    int left,
    int right,
    std::array<std::uint8_t, kTemplatePixels>& output
) noexcept {
    const int sourceWidth = right - left;

    for (int y = 0; y < kTemplateHeight; ++y) {
        const int sourceY = std::min(
            roi.height - 1,
            (y * roi.height) / kTemplateHeight
        );

        const auto* sourceRow =
            roi.pixels +
            static_cast<std::ptrdiff_t>(sourceY) * roi.stride +
            left;

        auto* targetRow =
            output.data() +
            static_cast<std::size_t>(y) * kTemplateWidth;

        for (int x = 0; x < kTemplateWidth; ++x) {
            const int sourceX = std::min(
                sourceWidth - 1,
                (x * sourceWidth) / kTemplateWidth
            );

            targetRow[x] = sourceRow[sourceX];
        }
    }
}

} // namespace


Recognizer::Recognizer()
    : avx2Available_{cpuHasAvx2()} {
}

Recognizer::~Recognizer() = default;


bool Recognizer::loadTemplates(
    const std::filesystem::path& directory,
    std::wstring& error
) {
    std::array<std::vector<Template>, 36> loaded{};

    std::error_code filesystemError;

    if (!std::filesystem::is_directory(
            directory,
            filesystemError)) {
        error = L"模板目录不存在：" + directory.wstring();
        return false;
    }

    for (std::size_t index = 0;
         index < kAlphabet.size();
         ++index) {

        const auto characterDirectory =
            directory / std::string(1, kAlphabet[index]);

        if (!std::filesystem::is_directory(
                characterDirectory,
                filesystemError)) {
            error =
                L"缺少模板目录：" +
                characterDirectory.wstring();
            return false;
        }

        for (const auto& entry :
             std::filesystem::directory_iterator(
                 characterDirectory,
                 filesystemError)) {

            if (filesystemError) {
                break;
            }

            if (!entry.is_regular_file() ||
                entry.path().extension() != L".bin") {
                continue;
            }

            std::ifstream file{
                entry.path(),
                std::ios::binary
            };

            std::array<char, 4> magic{};

            file.read(
                magic.data(),
                static_cast<std::streamsize>(magic.size())
            );

            if (!file || magic != kTemplateMagic) {
                error =
                    L"模板格式错误：" +
                    entry.path().wstring();
                return false;
            }

            const int format = file.peek();

            int width{};
            int height{};
            int bboxX{};
            int bboxY{};
            int bboxWidth{};
            int bboxHeight{};

            char value{};
            std::uint8_t background{};

            const bool textFormat = format == '\n';

            if (textFormat) {
                file.get();

                int backgroundValue{};

                file >>
                    width >>
                    height >>
                    bboxX >>
                    bboxY >>
                    bboxWidth >>
                    bboxHeight >>
                    value >>
                    backgroundValue;

                if (backgroundValue < 0 ||
                    backgroundValue > 255) {
                    error =
                        L"模板背景灰度无效：" +
                        entry.path().wstring();
                    return false;
                }

                background =
                    static_cast<std::uint8_t>(
                        backgroundValue
                    );

                file.ignore(
                    std::numeric_limits<
                        std::streamsize
                    >::max(),
                    '\n'
                );
            }
            else {
                width = readU16(file);
                height = readU16(file);

                bboxX = readU16(file);
                bboxY = readU16(file);

                bboxWidth = readU16(file);
                bboxHeight = readU16(file);

                file.read(&value, 1);

                file.read(
                    reinterpret_cast<char*>(&background),
                    1
                );
            }

            if (!file ||
                value != kAlphabet[index] ||
                width != kTemplateWidth ||
                height != kTemplateHeight ||
                bboxX < 0 ||
                bboxY < 0 ||
                bboxWidth <= 0 ||
                bboxHeight <= 0 ||
                bboxX + bboxWidth > width ||
                bboxY + bboxHeight > height) {

                error =
                    L"模板必须为 32x32，且头信息有效：" +
                    entry.path().wstring();

                return false;
            }

            Template templ{
                value,
                width,
                height,
                bboxX,
                bboxY,
                bboxWidth,
                bboxHeight,
                background,
                std::vector<std::uint8_t>(
                    kTemplatePixels
                )
            };

            if (textFormat) {
                std::size_t pixel{};
                char marker{};

                while (pixel < templ.pixels.size() &&
                       file.get(marker)) {

                    if (marker == '#') {
                        templ.pixels[pixel++] = 235;
                    }
                    else if (marker == '.') {
                        templ.pixels[pixel++] =
                            background;
                    }
                }

                if (pixel != templ.pixels.size()) {
                    error =
                        L"模板像素数据不完整：" +
                        entry.path().wstring();
                    return false;
                }
            }
            else {
                file.read(
                    reinterpret_cast<char*>(
                        templ.pixels.data()
                    ),
                    static_cast<std::streamsize>(
                        templ.pixels.size()
                    )
                );

                if (!file) {
                    error =
                        L"模板像素数据不完整：" +
                        entry.path().wstring();
                    return false;
                }
            }

            loaded[index].push_back(
                std::move(templ)
            );

            if (loaded[index].size() > 8) {
                error =
                    L"每个字符最多允许 8 个模板：" +
                    characterDirectory.wstring();
                return false;
            }
        }

        if (filesystemError ||
            loaded[index].empty()) {

            error = filesystemError
                ? L"无法枚举模板：" +
                    characterDirectory.wstring()
                : L"缺少模板文件：" +
                    characterDirectory.wstring();

            return false;
        }
    }

    templates_ = std::move(loaded);

    error.clear();

    return true;
}


void Recognizer::setConfig(
    const RecognitionConfig& config
) noexcept {
    config_ = config;
}


bool Recognizer::usingAvx2() const noexcept {
    return avx2Available_;
}


bool Recognizer::ready() const noexcept {
    return std::all_of(
        templates_.begin(),
        templates_.end(),
        [](const auto& entries) {
            return !entries.empty();
        }
    );
}


Candidate Recognizer::recognize(
    const GrayImageView& roi
) const {
    Candidate candidate{};

    if (!roi.valid() ||
        !ready() ||
        roi.width < 6) {
        return candidate;
    }

    bool allBoxes = true;
    bool edgeBoxes = true;
    bool allScores = true;
    bool probeOk = true;

    std::array<
        std::uint8_t,
        kTemplatePixels
    > normalizedSlot{};

    for (int slot = 0;
         slot < 6;
         ++slot) {

        const int left =
            (roi.width * slot) / 6;

        const int right =
            (roi.width * (slot + 1)) / 6;

        if (right <= left) {
            return Candidate{};
        }

        normalizeSlot(
            roi,
            left,
            right,
            normalizedSlot
        );

        float bestScore =
            -std::numeric_limits<float>::infinity();

        float secondScore =
            -std::numeric_limits<float>::infinity();

        const Template* bestTemplate{};

        for (int templateId = 0;
             templateId <
                static_cast<int>(
                    kAlphabet.size()
                );
             ++templateId) {

            if (!allowedInSlot(
                    slot,
                    kAlphabet[templateId])) {
                continue;
            }

            float characterBest =
                -std::numeric_limits<
                    float
                >::infinity();

            const Template*
                characterTemplate{};

            for (const Template& templ :
                 templates_[templateId]) {

                std::uint64_t difference{};

#if defined(_M_X64) || defined(_M_IX86)
                if (avx2Available_) {
                    difference = sadAvx2(
                        normalizedSlot.data(),
                        templ.pixels.data(),
                        kTemplatePixels
                    );
                }
                else
#endif
                {
                    difference = sadScalar(
                        normalizedSlot.data(),
                        templ.pixels.data(),
                        kTemplatePixels
                    );
                }

                const float score =
                    1.0F -
                    static_cast<float>(
                        difference
                    ) /
                    static_cast<float>(
                        255ULL *
                        kTemplatePixels
                    );

                if (score > characterBest) {
                    characterBest = score;
                    characterTemplate = &templ;
                }
            }

            if (characterBest > bestScore) {
                secondScore = bestScore;
                bestScore = characterBest;
                bestTemplate =
                    characterTemplate;
            }
            else if (
                characterBest >
                secondScore) {

                secondScore =
                    characterBest;
            }
        }

        if (bestTemplate == nullptr) {
            return Candidate{};
        }

        int minX = kTemplateWidth;
        int minY = kTemplateHeight;

        int maxX = -1;
        int maxY = -1;

        const int foregroundDelta =
            std::max(
                12,
                static_cast<int>(
                    std::lround(
                        config_
                            .backgroundThreshold /
                        2.0
                    )
                )
            );

        for (int y = 0;
             y < kTemplateHeight;
             ++y) {

            const auto* row =
                normalizedSlot.data() +
                static_cast<std::size_t>(y) *
                    kTemplateWidth;

            for (int x = 0;
                 x < kTemplateWidth;
                 ++x) {

                if (std::abs(
                        static_cast<int>(row[x]) -
                        static_cast<int>(
                            bestTemplate->background
                        )
                    ) > foregroundDelta) {

                    minX = std::min(minX, x);
                    minY = std::min(minY, y);

                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }

        const int bboxWidth =
            maxX >= minX
                ? maxX - minX + 1
                : 0;

        const int bboxHeight =
            maxY >= minY
                ? maxY - minY + 1
                : 0;

        const auto complete =
            [this](
                int observed,
                int expected
            ) noexcept {

                if (expected <= 0) {
                    return false;
                }

                const float tolerance =
                    static_cast<float>(
                        expected
                    ) *
                    static_cast<float>(
                        config_.bboxTolerance
                    );

                return std::abs(
                    static_cast<float>(
                        observed - expected
                    )
                ) <= tolerance;
            };

        const bool boxComplete =
            complete(
                bboxWidth,
                bestTemplate->bboxWidth
            ) &&
            complete(
                bboxHeight,
                bestTemplate->bboxHeight
            );

        allBoxes =
            allBoxes &&
            boxComplete;

        if (slot == 0 ||
            slot == 5) {

            edgeBoxes =
                edgeBoxes &&
                boxComplete;

            const int startX =
                slot == 0
                    ? 0
                    : kTemplateWidth -
                        kProbeColumns;

            const int endX =
                slot == 0
                    ? kProbeColumns
                    : kTemplateWidth;

            std::uint64_t difference{};
            std::size_t samples{};

            for (int y = 0;
                 y < kTemplateHeight;
                 ++y) {

                const auto* row =
                    normalizedSlot.data() +
                    static_cast<std::size_t>(y) *
                        kTemplateWidth;

                for (int x = startX;
                     x < endX;
                     ++x) {

                    difference +=
                        static_cast<unsigned>(
                            std::abs(
                                static_cast<int>(
                                    row[x]
                                ) -
                                static_cast<int>(
                                    bestTemplate
                                        ->background
                                )
                            )
                        );

                    ++samples;
                }
            }

            const double meanDifference =
                samples == 0
                    ? std::numeric_limits<
                        double
                    >::infinity()
                    : static_cast<double>(
                        difference
                    ) /
                    static_cast<double>(
                        samples
                    );

            probeOk =
                probeOk &&
                meanDifference <=
                    config_
                        .backgroundThreshold;
        }

        const float safeSecond =
            std::max(
                0.0F,
                secondScore
            );

        auto& result =
            candidate.slots[slot];

        result = MatchResult{
            bestTemplate->value,
            bestScore,
            safeSecond,
            bestScore - safeSecond,
            bboxWidth,
            bboxHeight
        };

        candidate.code.push_back(
            result.value
        );

        allScores =
            allScores &&
            bestScore >=
                config_.scoreThreshold &&
            result.margin >=
                config_.marginThreshold;
    }

    candidate.structureValid =
        candidate.code.size() == 6 &&

        std::all_of(
            candidate.code.begin(),
            candidate.code.begin() + 3,
            [](char c) {
                return c >= 'A' &&
                       c <= 'Z';
            }
        ) &&

        std::all_of(
            candidate.code.begin() + 3,
            candidate.code.end(),
            [](char c) {
                return c >= '0' &&
                       c <= '9';
            }
        );

    candidate.boundingBoxesComplete =
        allBoxes;

    candidate.edgeSlotsComplete =
        edgeBoxes;

    candidate.backgroundProbeOk =
        probeOk;

    candidate.highConfidence =
        candidate.structureValid &&
        allScores &&
        allBoxes &&
        edgeBoxes &&
        probeOk;

    return candidate;
}


Candidate Recognizer::evaluate(
    std::string_view code
) const {
    Candidate candidate{};

    candidate.code = code;

    candidate.structureValid =
        code.size() == 6 &&

        std::all_of(
            code.begin(),
            code.begin() + 3,
            [](char c) {
                return c >= 'A' &&
                       c <= 'Z';
            }
        ) &&

        std::all_of(
            code.begin() + 3,
            code.end(),
            [](char c) {
                return c >= '0' &&
                       c <= '9';
            }
        );

    return candidate;
}


bool Recognizer::shouldSubmit(
    const Candidate& candidate,
    const std::optional<std::string>& previous
) const {
    if (!candidate.structureValid ||
        !candidate.boundingBoxesComplete ||
        !candidate.edgeSlotsComplete ||
        !candidate.backgroundProbeOk) {

        return false;
    }

    if (candidate.highConfidence) {
        return true;
    }

    return previous &&
           *previous == candidate.code;
}

} // namespace valinvite