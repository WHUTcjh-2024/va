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
constexpr std::array<char, 36> kAlphabet{'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','0','1','2','3','4','5','6','7','8','9'};
constexpr std::array<char, 4> kTemplateMagic{'V', 'I', 'T', '1'};

[[nodiscard]] bool allowedInSlot(int slot, char value) noexcept { return slot < 3 ? value >= 'A' && value <= 'Z' : value >= '0' && value <= '9'; }
[[nodiscard]] std::uint16_t readU16(std::istream& stream) {
    std::array<unsigned char, 2> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
}
[[nodiscard]] std::uint64_t sadScalar(const std::uint8_t* image, const std::uint8_t* templ, std::size_t count) noexcept {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) total += static_cast<unsigned>(std::abs(static_cast<int>(image[i]) - static_cast<int>(templ[i])));
    return total;
}
#if defined(_M_X64) || defined(_M_IX86)
[[nodiscard]] std::uint64_t sadAvx2(const std::uint8_t* image, const std::uint8_t* templ, std::size_t count) noexcept {
    std::uint64_t total{}; std::size_t i{};
    for (; i + 32 <= count; i += 32) {
        const __m256i partial = _mm256_sad_epu8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(image + i)), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(templ + i)));
        alignas(32) std::array<std::uint64_t, 4> lanes{};
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes.data()), partial);
        total += lanes[0] + lanes[1] + lanes[2] + lanes[3];
    }
    return total + sadScalar(image + i, templ + i, count - i);
}
#endif
[[nodiscard]] bool cpuHasAvx2() noexcept {
#if defined(_M_X64) || defined(_M_IX86)
    int info[4]{}; __cpuidex(info, 0, 0); if (info[0] < 7) return false;
    __cpuidex(info, 1, 0); if ((info[2] & (1 << 27)) == 0 || (info[2] & (1 << 28)) == 0) return false;
    if ((_xgetbv(0) & 0x6U) != 0x6U) return false;
    __cpuidex(info, 7, 0); return (info[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}
[[nodiscard]] int scaledCoordinate(int sourceSize, int templateCoordinate, int templateSize) noexcept { return std::min(sourceSize - 1, (templateCoordinate * sourceSize) / templateSize); }
} // namespace

Recognizer::Recognizer() : avx2Available_{cpuHasAvx2()} { std::wstring ignored; (void)loadTemplates(L"templates", ignored); }
Recognizer::~Recognizer() = default;

bool Recognizer::loadTemplates(const std::filesystem::path& directory, std::wstring& error) {
    std::array<std::vector<Template>, 36> loaded{};
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(directory, filesystemError)) { error = L"模板目录不存在：" + directory.wstring(); return false; }
    for (std::size_t index = 0; index < kAlphabet.size(); ++index) {
        const auto characterDirectory = directory / std::string(1, kAlphabet[index]);
        if (!std::filesystem::is_directory(characterDirectory, filesystemError)) { error = L"缺少模板目录：" + characterDirectory.wstring(); return false; }
        for (const auto& entry : std::filesystem::directory_iterator(characterDirectory, filesystemError)) {
            if (filesystemError) break;
            if (!entry.is_regular_file() || entry.path().extension() != L".bin") continue;
            std::ifstream file{entry.path(), std::ios::binary}; std::array<char, 4> magic{};
            file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (!file || magic != kTemplateMagic) { error = L"模板格式错误：" + entry.path().wstring(); return false; }
            const int format = file.peek();
            int width{}, height{}, bboxX{}, bboxY{}, bboxWidth{}, bboxHeight{}; char value{}; std::uint8_t background{};
            const bool textFormat = format == '\n';
            if (textFormat) {
                file.get(); int backgroundValue{};
                file >> width >> height >> bboxX >> bboxY >> bboxWidth >> bboxHeight >> value >> backgroundValue;
                background = static_cast<std::uint8_t>(backgroundValue);
                file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            } else {
                width = readU16(file); height = readU16(file); bboxX = readU16(file); bboxY = readU16(file); bboxWidth = readU16(file); bboxHeight = readU16(file);
                file.read(&value, 1); file.read(reinterpret_cast<char*>(&background), 1);
            }
            if (!file || value != kAlphabet[index] || width <= 0 || height <= 0 || bboxWidth <= 0 || bboxHeight <= 0 || bboxX + bboxWidth > width || bboxY + bboxHeight > height) { error = L"模板头信息无效：" + entry.path().wstring(); return false; }
            Template templ{value, width, height, bboxX, bboxY, bboxWidth, bboxHeight, background, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * static_cast<std::size_t>(height))};
            if (textFormat) {
                std::size_t pixel{}; char marker{};
                while (pixel < templ.pixels.size() && file.get(marker)) {
                    if (marker == '#') templ.pixels[pixel++] = 235;
                    else if (marker == '.') templ.pixels[pixel++] = background;
                }
            } else file.read(reinterpret_cast<char*>(templ.pixels.data()), static_cast<std::streamsize>(templ.pixels.size()));
            if (!file) { error = L"模板像素数据不完整：" + entry.path().wstring(); return false; }
            loaded[index].push_back(std::move(templ));
            if (loaded[index].size() > 8) { error = L"每个字符最多允许 8 个模板：" + characterDirectory.wstring(); return false; }
        }
        if (filesystemError || loaded[index].empty()) { error = filesystemError ? L"无法枚举模板：" + characterDirectory.wstring() : L"缺少模板文件：" + characterDirectory.wstring(); return false; }
    }
    templates_ = std::move(loaded); error.clear(); return true;
}

void Recognizer::setConfig(const RecognitionConfig& config) noexcept { config_ = config; }
bool Recognizer::usingAvx2() const noexcept { return avx2Available_; }
bool Recognizer::ready() const noexcept { return std::all_of(templates_.begin(), templates_.end(), [](const auto& entries) { return !entries.empty(); }); }

Candidate Recognizer::recognize(const GrayImageView& roi) const {
    Candidate candidate{};
    if (!roi.valid() || !ready() || roi.width < 6) return candidate;
    bool allBoxes = true, allScores = true, probeOk = true;
    for (int slot = 0; slot < 6; ++slot) {
        const int left = (roi.width * slot) / 6, right = (roi.width * (slot + 1)) / 6, slotWidth = right - left;
        float bestScore = -std::numeric_limits<float>::infinity(), secondScore = -std::numeric_limits<float>::infinity(); const Template* bestTemplate{};
        for (int templateId = 0; templateId < static_cast<int>(kAlphabet.size()); ++templateId) {
            if (!allowedInSlot(slot, kAlphabet[templateId])) continue;
            for (const Template& templ : templates_[templateId]) {
                std::uint64_t difference{}; const bool sameShape = slotWidth == templ.width && roi.height == templ.height;
                if (sameShape && avx2Available_) {
#if defined(_M_X64) || defined(_M_IX86)
                    for (int y = 0; y < roi.height; ++y) difference += sadAvx2(roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride + left, templ.pixels.data() + static_cast<std::size_t>(y) * templ.width, static_cast<std::size_t>(templ.width));
#else
                    for (int y = 0; y < roi.height; ++y) difference += sadScalar(roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride + left, templ.pixels.data() + static_cast<std::size_t>(y) * templ.width, static_cast<std::size_t>(templ.width));
#endif
                } else {
                    for (int y = 0; y < templ.height; ++y) {
                        const int sourceY = scaledCoordinate(roi.height, y, templ.height); const auto* sourceRow = roi.pixels + static_cast<std::ptrdiff_t>(sourceY) * roi.stride + left; const auto* templateRow = templ.pixels.data() + static_cast<std::size_t>(y) * templ.width;
                        for (int x = 0; x < templ.width; ++x) { const int sourceX = scaledCoordinate(slotWidth, x, templ.width); difference += static_cast<unsigned>(std::abs(static_cast<int>(sourceRow[sourceX]) - static_cast<int>(templateRow[x]))); }
                    }
                }
                const float score = 1.0F - static_cast<float>(difference) / static_cast<float>(255ULL * templ.pixels.size());
                if (score > bestScore) { secondScore = bestScore; bestScore = score; bestTemplate = &templ; } else if (score > secondScore) secondScore = score;
            }
        }
        if (bestTemplate == nullptr) return Candidate{};
        int minX = slotWidth, minY = roi.height, maxX = -1, maxY = -1; const int foregroundDelta = std::max(12, static_cast<int>(std::lround(config_.backgroundThreshold / 2.0)));
        for (int y = 0; y < roi.height; ++y) { const auto* row = roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride + left; for (int x = 0; x < slotWidth; ++x) if (std::abs(static_cast<int>(row[x]) - static_cast<int>(bestTemplate->background)) > foregroundDelta) { minX = std::min(minX, x); minY = std::min(minY, y); maxX = std::max(maxX, x); maxY = std::max(maxY, y); } }
        const int bboxWidth = maxX >= minX ? maxX - minX + 1 : 0, bboxHeight = maxY >= minY ? maxY - minY + 1 : 0;
        const float expectedWidth = static_cast<float>(bestTemplate->bboxWidth) * slotWidth / bestTemplate->width, expectedHeight = static_cast<float>(bestTemplate->bboxHeight) * roi.height / bestTemplate->height;
        const auto complete = [this](int observed, float expected) { return expected > 0.0F && std::abs(static_cast<float>(observed) - expected) <= expected * static_cast<float>(config_.bboxTolerance); };
        const bool boxComplete = complete(bboxWidth, expectedWidth) && complete(bboxHeight, expectedHeight);
        if (slot == 0 || slot == 5) {
            const int startX = slot == 0 ? 0 : std::max(0, slotWidth - std::max(1, slotWidth / 8)); const int endX = slot == 0 ? std::max(1, slotWidth / 8) : slotWidth; std::uint64_t difference{}; std::size_t samples{};
            for (int y = 0; y < roi.height; ++y) { const auto* row = roi.pixels + static_cast<std::ptrdiff_t>(y) * roi.stride + left; for (int x = startX; x < endX; ++x) { difference += static_cast<unsigned>(std::abs(static_cast<int>(row[x]) - static_cast<int>(bestTemplate->background))); ++samples; } }
            probeOk = probeOk && samples != 0 && static_cast<double>(difference) / samples <= config_.backgroundThreshold;
        }
        auto& result = candidate.slots[slot]; result = MatchResult{bestTemplate->value, bestScore, std::max(0.0F, secondScore), bestScore - std::max(0.0F, secondScore), bboxWidth, bboxHeight}; candidate.code.push_back(result.value);
        allBoxes = allBoxes && boxComplete; allScores = allScores && bestScore >= config_.scoreThreshold && result.margin >= config_.marginThreshold;
    }
    candidate.structureValid = candidate.code.size() == 6 && std::all_of(candidate.code.begin(), candidate.code.begin() + 3, [](char c) { return c >= 'A' && c <= 'Z'; }) && std::all_of(candidate.code.begin() + 3, candidate.code.end(), [](char c) { return c >= '0' && c <= '9'; });
    candidate.boundingBoxesComplete = allBoxes; candidate.edgeSlotsComplete = candidate.slots[0].bboxWidth > 0 && candidate.slots[0].bboxHeight > 0 && candidate.slots[5].bboxWidth > 0 && candidate.slots[5].bboxHeight > 0; candidate.backgroundProbeOk = probeOk;
    candidate.highConfidence = candidate.structureValid && allScores && allBoxes && candidate.edgeSlotsComplete && probeOk;
    return candidate;
}

Candidate Recognizer::evaluate(std::string_view code) const {
    Candidate candidate{}; candidate.code = code;
    candidate.structureValid = code.size() == 6 && std::all_of(code.begin(), code.begin() + 3, [](char c) { return c >= 'A' && c <= 'Z'; }) && std::all_of(code.begin() + 3, code.end(), [](char c) { return c >= '0' && c <= '9'; });
    for (std::size_t index = 0; index < candidate.slots.size() && index < code.size(); ++index) candidate.slots[index] = MatchResult{code[index], 1.0F, 0.0F, 1.0F, 1, 1};
    candidate.boundingBoxesComplete = candidate.structureValid; candidate.edgeSlotsComplete = candidate.structureValid; candidate.backgroundProbeOk = candidate.structureValid; candidate.highConfidence = candidate.structureValid; return candidate;
}

bool Recognizer::shouldSubmit(const Candidate& candidate, const std::optional<std::string>& previous) const {
    if (!candidate.structureValid || !candidate.boundingBoxesComplete || !candidate.edgeSlotsComplete || !candidate.backgroundProbeOk) return false;
    return candidate.highConfidence || (previous && *previous == candidate.code);
}
} // namespace valinvite
