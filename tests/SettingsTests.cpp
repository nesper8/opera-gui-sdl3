#include "app/Settings.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testDefaultsAndRoundTrip() {
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("opera-sdl3-settings-test-" + std::to_string(uniqueId));
    std::filesystem::create_directories(directory);
    const auto file = directory / "settings.ini";
    try {
        app::Settings settings = app::Settings::defaults();
        require(settings.videoDriver == "opengl", "OpenGL must be the default video driver.");
        require(settings.audioDriver == "wasapi", "WASAPI must be the default audio driver.");
        require(settings.keyboardBindings[0][0] != 0, "Default keyboard bindings must be populated.");
        require(settings.gamepadBindings[0][0] == 1 && settings.gamepadBindings[0][1] == 0 &&
                    settings.gamepadBindings[0][8] == 2,
                "Default gamepad bindings must match the 3DO B, A, and C labels.");
        settings.videoDriver = "vulkan";
        settings.gpuPreference = "high_performance";
        settings.scaleMode = "linear";
        settings.shaderPreset = "crt";
        settings.audioDriver = "default";
        settings.audioGain = 0.75F;
        settings.gamepadGuids[1] = "test-guid#1";
        settings.keyboardBindings[1][app::PadButtonCount - 1] = 77;
        settings.gamepadBindings[1][1] = 2;
        settings.gamepadBindings[1][12] = -6;
        settings.coreOptions["opera_region"] = "NTSC";
        settings.save(file);

        const app::Settings loaded = app::Settings::load(file);
        require(loaded.videoDriver == "vulkan", "Video driver did not persist.");
        require(loaded.gpuPreference == "high_performance", "GPU preference did not persist.");
        require(loaded.scaleMode == "linear", "Scale mode did not persist.");
        require(loaded.shaderPreset == "crt", "Shader preset did not persist.");
        require(loaded.audioDriver == "default", "Audio driver did not persist.");
        require(loaded.audioGain == 0.75F, "Audio gain did not persist.");
        require(loaded.gamepadGuids[1] == "test-guid#1", "Gamepad selection did not persist.");
        require(loaded.keyboardBindings[1][app::PadButtonCount - 1] == 77, "Keyboard mapping did not persist.");
        require(loaded.gamepadBindings[1][1] == 2, "Gamepad button mapping did not persist.");
        require(loaded.gamepadBindings[1][12] == -6, "Gamepad trigger mapping did not persist.");
        require(loaded.coreOptions.at("opera_region") == "NTSC", "Core option did not persist.");

        {
            std::ofstream output(file, std::ios::trunc);
            output << "[video]\ndriver=unknown\ngpu=unknown\n[audio]\ngain=nan\n";
        }
        const app::Settings sanitized = app::Settings::load(file);
        require(sanitized.videoDriver == "opengl", "Unknown renderer must fall back to OpenGL.");
        require(sanitized.gpuPreference == "system", "Unknown GPU preference must fall back to system default.");
        require(sanitized.audioGain == 1.0F, "Non-finite gain must fall back to its default.");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        throw;
    }
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    try {
        testDefaultsAndRoundTrip();
        std::cout << "Settings tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Settings tests failed: " << error.what() << '\n';
        return 1;
    }
}
