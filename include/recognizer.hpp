#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

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

    Recognizer(const Recognizer&) = delete;
    Recognizer& operator=(const Recognizer&) = delete;

    void setConfig(const RecognitionConfig& config) noexcept;
    [[nodiscard]] bool ready() const noexcept;

    // Segment the complete ROI into exactly six foreground runs, normalize
    // each glyph independently while preserving aspect ratio, then match the
    // compile-time A-Z/0-9 atlas. Any other run count is treated as no code.
    [[nodiscard]] Candidate recognize(const GrayImageView& roi) const;

    // Syntax-only helper. It deliberately does not manufacture image gates;
    // only recognize() can create a submit-eligible Candidate.
    [[nodiscard]] Candidate evaluate(std::string_view code) const;
    [[nodiscard]] bool shouldSubmit(const Candidate& candidate, const std::optional<std::string>& previous) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RecognitionConfig config_{};
};

} // namespace valinvite
