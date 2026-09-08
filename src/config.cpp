#include "config.hpp"

#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace valinvite {
namespace {

std::optional<double> number(const std::string& json, const std::string& key) {
    const std::regex expression{"\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)"};
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    return std::stod(match[1].str());
}

std::optional<bool> boolean(const std::string& json, const std::string& key) {
    const std::regex expression{"\\\"" + key + "\\\"\\s*:\\s*(true|false)"};
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    return match[1].str() == "true";
}

std::optional<NormalizedRect> normalizedRoi(const std::string& json) {
    const std::regex expression{R"("roi_ratio"\s*:\s*\{"x"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)\s*,\s*"y"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)\s*,\s*"w"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)\s*,\s*"h"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)\})"};
    std::smatch match;
    if (!std::regex_search(json, match, expression)) return std::nullopt;
    NormalizedRect result{std::stod(match[1].str()), std::stod(match[2].str()), std::stod(match[3].str()), std::stod(match[4].str())};
    return result.valid() ? std::optional<NormalizedRect>{result} : std::nullopt;
}

} // namespace

ConfigStore::ConfigStore(std::filesystem::path path) : path_{std::move(path)} {}

bool ConfigStore::load(Config& config, std::wstring& error) const {
    std::ifstream file{path_};
    if (!file) {
        error = L"无法打开 config.json";
        return false;
    }
    const std::string json{std::istreambuf_iterator<char>{file}, {}};
    const auto score = number(json, "score_threshold");
    const auto margin = number(json, "margin_threshold");
    const auto bbox = number(json, "bbox_tolerance");
    const auto background = number(json, "background_threshold");
    const auto inputX = number(json, "input_x");
    const auto inputY = number(json, "input_y");
    const auto joinX = number(json, "join_x");
    const auto joinY = number(json, "join_y");
    const auto affinity = number(json, "cpu_affinity");
    const auto priority = boolean(json, "thread_priority_highest");
    const auto captureRoi = normalizedRoi(json);
    if (!score || !margin || !bbox || !background || !inputX || !inputY || !joinX || !joinY || !affinity || !priority || !captureRoi) {
        error = L"config.json 缺少识别阈值";
        return false;
    }
    config.recognition.scoreThreshold = *score;
    config.recognition.marginThreshold = *margin;
    config.recognition.bboxTolerance = *bbox;
    config.recognition.backgroundThreshold = *background;
    config.normalizedRoi = *captureRoi;
    config.roi = {};
    config.inputPoint = {static_cast<int>(*inputX), static_cast<int>(*inputY)};
    config.joinPoint = {static_cast<int>(*joinX), static_cast<int>(*joinY)};
    config.cpuAffinity = static_cast<int>(*affinity);
    config.highPriority = *priority;
    config.submitMode = json.find("\"submit_mode\": \"click\"") != std::string::npos
        ? SubmitMode::ClickJoin : SubmitMode::Enter;
    return true;
}

bool ConfigStore::save(const Config& config, std::wstring& error) const {
    std::error_code directoryError;
    const auto parent = path_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
        error = L"无法创建用户配置目录";
        return false;
    }
    std::ofstream file{path_, std::ios::trunc};
    if (!file) {
        error = L"无法写入 config.json";
        return false;
    }
    file << std::fixed << std::setprecision(8)
         << "{\n  \"capture\": {\n    \"window_title\": \"\",\n"
         << "    \"roi_ratio\": {\"x\": " << config.normalizedRoi.x << ", \"y\": " << config.normalizedRoi.y
         << ", \"w\": " << config.normalizedRoi.width << ", \"h\": " << config.normalizedRoi.height << "}\n  },\n"
         << "  \"recognition\": {\n    \"score_threshold\": " << config.recognition.scoreThreshold
         << ",\n    \"margin_threshold\": " << config.recognition.marginThreshold
         << ",\n    \"bbox_tolerance\": " << config.recognition.bboxTolerance
         << ",\n    \"background_threshold\": " << config.recognition.backgroundThreshold << "\n  },\n"
         << "  \"game\": {\n    \"input_x\": " << config.inputPoint.x << ", \"input_y\": " << config.inputPoint.y
         << ",\n    \"join_x\": " << config.joinPoint.x << ", \"join_y\": " << config.joinPoint.y
         << ",\n    \"submit_mode\": \"" << (config.submitMode == SubmitMode::Enter ? "enter" : "click") << "\"\n  },\n"
         << "  \"performance\": {\n    \"thread_priority_highest\": " << (config.highPriority ? "true" : "false")
         << ",\n    \"cpu_affinity\": " << config.cpuAffinity << "\n  }\n}\n";
    if (!file) {
        error = L"写入 config.json 时发生错误";
        return false;
    }
    return true;
}

} // namespace valinvite
