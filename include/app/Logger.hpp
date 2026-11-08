#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

namespace app {

enum class LogLevel { Info, Warning, Error };

class Logger final {
public:
    static void initialize(const std::filesystem::path& file);
    static void write(LogLevel level, std::string_view message) noexcept;
};

} // namespace app
