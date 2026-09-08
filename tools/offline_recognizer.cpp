#include "recognizer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::optional<int> parseDimension(const wchar_t* text) noexcept {
    try {
        std::size_t consumed{};
        const long value = std::stol(text, &consumed, 10);
        if (text[consumed] != L'\0' || value <= 0 ||
            value > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    }
    catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> parseCode(std::wstring_view text) {
    if (text.size() != 6) {
        return std::nullopt;
    }

    std::string code;
    code.reserve(text.size());
    for (const wchar_t value : text) {
        if (value > 0x7f) {
            return std::nullopt;
        }
        code.push_back(static_cast<char>(value));
    }

    const bool valid =
        code[0] >= 'A' && code[0] <= 'Z' &&
        code[1] >= 'A' && code[1] <= 'Z' &&
        code[2] >= 'A' && code[2] <= 'Z' &&
        code[3] >= '0' && code[3] <= '9' &&
        code[4] >= '0' && code[4] <= '9' &&
        code[5] >= '0' && code[5] <= '9';

    return valid ? std::optional{std::move(code)} : std::nullopt;
}

void printBoolean(std::string_view name, bool value) {
    std::cout << name << ": " << (value ? "true" : "false") << '\n';
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 5) {
        std::cerr
            << "Usage: VALInviteOffline <raw-gray> <width> <height> "
               "<expected-code>\n";
        return 2;
    }

    const auto width = parseDimension(argv[2]);
    const auto height = parseDimension(argv[3]);
    const auto expected = parseCode(argv[4]);
    if (!width || !height || !expected) {
        std::cerr << "Invalid dimensions or expected code (must match AAA999).\n";
        return 2;
    }

    const std::uint64_t expectedBytes =
        static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height);
    if (expectedBytes > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "ROI is too large.\n";
        return 2;
    }

    std::ifstream raw{std::filesystem::path{argv[1]}, std::ios::binary};
    if (!raw) {
        std::cerr << "Cannot open raw grayscale ROI.\n";
        return 2;
    }

    std::vector<std::uint8_t> pixels{
        std::istreambuf_iterator<char>{raw},
        std::istreambuf_iterator<char>{}
    };
    if (pixels.size() != static_cast<std::size_t>(expectedBytes)) {
        std::cerr << "Raw ROI size mismatch: got " << pixels.size()
                  << " bytes, expected " << expectedBytes << ".\n";
        return 2;
    }

    valinvite::Recognizer recognizer;
    recognizer.setConfig(valinvite::RecognitionConfig{});
    const valinvite::Candidate candidate = recognizer.recognize({
        pixels.data(), *width, *height, *width
    });

    const bool submitSingle = recognizer.shouldSubmit(candidate, std::nullopt);
    const bool submitRepeated = recognizer.shouldSubmit(candidate, candidate.code);
    const bool pass = candidate.code == *expected && submitRepeated;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Expected: " << *expected << '\n';
    std::cout << "Recognized: " << candidate.code << '\n';
    for (std::size_t slot = 0; slot < candidate.slots.size(); ++slot) {
        const auto& result = candidate.slots[slot];
        std::cout << "Slot " << slot << ": best=" << result.value
                  << " score=" << result.bestScore
                  << " second=" << result.secondScore
                  << " margin=" << result.margin
                  << " x=" << result.xStart << '-' << result.xEnd
                  << " bbox=" << result.bboxWidth << 'x' << result.bboxHeight
                  << '\n';
    }

    printBoolean("structureValid", candidate.structureValid);
    printBoolean("boundingBoxesComplete", candidate.boundingBoxesComplete);
    printBoolean("edgeSlotsComplete", candidate.edgeSlotsComplete);
    printBoolean("backgroundProbeOk", candidate.backgroundProbeOk);
    printBoolean("codeVisible", candidate.codeVisible);
    printBoolean("highConfidence", candidate.highConfidence);
    printBoolean("shouldSubmit(singleFrame)", submitSingle);
    printBoolean("shouldSubmit(twoFrames)", submitRepeated);
    std::cout << (pass ? "PASS" : "FAIL") << '\n';
    return pass ? 0 : 1;
}
