#pragma once

#include <filesystem>

namespace app {

class CoreDownloader final {
public:
    std::filesystem::path downloadAndExtract(const std::filesystem::path& coreDirectory) const;
};

} // namespace app
