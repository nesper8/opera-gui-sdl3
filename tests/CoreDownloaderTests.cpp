#include "app/CoreDownloader.hpp"
#include "app/LibretroCore.hpp"
#include "app/Logger.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("opera-core-download-test-" + std::to_string(uniqueId));
    try {
        app::Logger::initialize(directory / "test.log");
        const auto installed = app::CoreDownloader{}.downloadAndExtract(directory);
        if (!std::filesystem::is_regular_file(installed) || std::filesystem::file_size(installed) == 0) {
            throw std::runtime_error("Core downloader did not install a non-empty DLL.");
        }
        {
            app::LibretroCore core(directory / "system", directory / "saves", {});
            core.load(installed);
            if (core.coreName() != "Opera") {
                throw std::runtime_error("Downloaded DLL did not initialize as the Opera core.");
            }
        }
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cout << "Core download and extraction succeeded.\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << "Core downloader test failed: " << error.what() << '\n';
        return 1;
    }
}
