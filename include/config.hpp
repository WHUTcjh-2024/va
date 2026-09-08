#pragma once

#include "types.hpp"

#include <filesystem>

namespace valinvite {

class ConfigStore final {
public:
    explicit ConfigStore(std::filesystem::path path);

    [[nodiscard]] bool load(Config& config, std::wstring& error) const;
    [[nodiscard]] bool save(const Config& config, std::wstring& error) const;

private:
    std::filesystem::path path_;
};

} // namespace valinvite
