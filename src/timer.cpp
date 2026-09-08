#include "timer.hpp"

#include <algorithm>
#include <cmath>

#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace valinvite {
namespace {
constexpr std::array<std::string_view, 3> kMetricNames{"recognition", "decision", "frame_to_input_dispatch"};

[[nodiscard]] double quantile(std::vector<double> values, double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = percentile * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double>(lower));
}
} // namespace

HighResolutionTimer::HighResolutionTimer() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    ticksPerMillisecond_ = static_cast<double>(frequency.QuadPart) / 1000.0;
    for (std::size_t index = 0; index < series_.size(); ++index) series_[index].name = kMetricNames[index];
}

double HighResolutionTimer::elapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const noexcept { return static_cast<double>(end.QuadPart - start.QuadPart) / ticksPerMillisecond_; }
LARGE_INTEGER HighResolutionTimer::now() const noexcept { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value; }

MemorySnapshot HighResolutionTimer::memory() const noexcept {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return {};
    return {static_cast<std::size_t>(counters.WorkingSetSize), static_cast<std::size_t>(counters.PrivateUsage), static_cast<std::size_t>(counters.PeakWorkingSetSize)};
}

void HighResolutionTimer::reserveSamples(std::size_t count) { for (auto& entry : series_) entry.values.reserve(count); }
HighResolutionTimer::Series* HighResolutionTimer::series(std::string_view metric) noexcept { for (auto& entry : series_) if (entry.name == metric) return &entry; return nullptr; }
const HighResolutionTimer::Series* HighResolutionTimer::series(std::string_view metric) const noexcept { for (const auto& entry : series_) if (entry.name == metric) return &entry; return nullptr; }
void HighResolutionTimer::record(std::string_view metric, double milliseconds) { if (Series* entry = series(metric); entry != nullptr && milliseconds >= 0.0) entry->values.push_back(milliseconds); }
LatencyPercentiles HighResolutionTimer::percentiles(std::string_view metric) const { const Series* entry = series(metric); if (entry == nullptr) return {}; return {quantile(entry->values, 0.50), quantile(entry->values, 0.95), quantile(entry->values, 0.99), entry->values.size()}; }
void HighResolutionTimer::clear() noexcept { for (auto& entry : series_) entry.values.clear(); }
} // namespace valinvite
