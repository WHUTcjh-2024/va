#include "recognizer.hpp"

#include <algorithm>
#include <cctype>

namespace valinvite {

Candidate Recognizer::evaluate(std::string_view code) const {
    Candidate candidate{};
    candidate.code = code;
    candidate.structureValid = code.size() == 6
        && std::all_of(code.begin(), code.begin() + 3, [](unsigned char c) { return c >= 'A' && c <= 'Z'; })
        && std::all_of(code.begin() + 3, code.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    return candidate;
}

bool Recognizer::shouldSubmit(const Candidate& candidate, const std::optional<std::string>& previous) const {
    if (!candidate.structureValid || !candidate.boundingBoxesComplete || !candidate.backgroundProbeOk) return false;
    return candidate.highConfidence || (previous && *previous == candidate.code);
}

} // namespace valinvite
