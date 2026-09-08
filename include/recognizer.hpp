#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace valinvite {

struct GrayImageView final {
    // One byte per pixel grayscale ROI. Convert BGRA at capture time with
    // (77 * R + 150 * G + 29 * B + 128) >> 8; alpha is ignored. `stride` is
    // byte distance between rows and may exceed width. The caller retains
    // ownership and must keep pixels alive until recognize() returns.
    const std::uint8_t* pixels{};
    int width{};
    int height{};
    std::ptrdiff_t stride{};

    [[nodiscard]] bool valid() const noexcept {
        return pixels != nullptr && width > 0 && height > 0 && stride >= width;
    }
};

class Recognizer final {
public:
    Recognizer();
    ~Recognizer();

    [[nodiscard]] bool loadTemplates(const std::filesystem::path& directory, std::wstring& error);
    void setConfig(const RecognitionConfig& config) noexcept;
    [[nodiscard]] bool usingAvx2() const noexcept;
    [[nodiscard]] bool ready() const noexcept;

    // ROI is divided into six equal logical slots. The remainder pixels are
    // distributed from left to right, so every ROI pixel belongs to one slot.
    [[nodiscard]] Candidate recognize(const GrayImageView& roi) const;

    // Syntax-only helper. It deliberately does not manufacture image gates;
    // only recognize() can create a submit-eligible Candidate.
    [[nodiscard]] Candidate evaluate(std::string_view code) const;
    [[nodiscard]] bool shouldSubmit(const Candidate& candidate, const std::optional<std::string>& previous) const;
private:
    struct Template final {
        char value{};
        int width{};
        int height{};
        int bboxX{};
        int bboxY{};
        int bboxWidth{};
        int bboxHeight{};
        std::uint8_t background{};
        std::vector<std::uint8_t> pixels;
    };
    std::array<std::vector<Template>, 36> templates_{};
    RecognitionConfig config_{};
    bool avx2Available_{};
};

} // namespace valinvite
