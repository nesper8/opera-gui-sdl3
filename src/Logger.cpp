#include "app/Logger.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace app {
namespace {

std::mutex logMutex;
std::ofstream logFile;

const char* levelName(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace

void Logger::initialize(const std::filesystem::path& file) {
    std::scoped_lock lock(logMutex);
    std::filesystem::create_directories(file.parent_path());
    logFile.open(file, std::ios::out | std::ios::app);
    if (!logFile) {
        throw std::runtime_error("Unable to open the application log file.");
    }
}

void Logger::write(const LogLevel level, const std::string_view message) noexcept {
    try {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::scoped_lock lock(logMutex);
        const std::string line = std::string(levelName(level)) + ": " + std::string(message);
        std::clog << line << '\n';
        if (logFile) {
            logFile << time << ' ' << line << '\n';
            logFile.flush();
        }
    } catch (...) {
        // Logging must not allow an exception to cross a libretro callback.
    }
}

} // namespace app
