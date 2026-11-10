#include "app/settings/Settings.hpp"

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
        require(settings.fullscreenMode == "borderless",
                "Borderless windowed must be the default fullscreen mode.");
        require(settings.audioDriver == "default", "SDL default must be the default audio driver.");
        require(settings.audioLatencyMs == 40, "Audio latency must default to 40 ms.");
        require(!settings.keyboardBindings[0][0].empty(), "Default keyboard bindings must be populated.");
        require(settings.gamepadBindings[0][0] == std::vector<int>{1} &&
                    settings.gamepadBindings[0][1] == std::vector<int>{0} &&
                    settings.gamepadBindings[0][8] == std::vector<int>{3},
                "Default face-button bindings must match the screenshot.");
        require(settings.gamepadBindings[0][4] == std::vector<int>({11, -103}) &&
                    settings.gamepadBindings[0][5] == std::vector<int>({12, -102}) &&
                    settings.gamepadBindings[0][6] == std::vector<int>({13, -101}) &&
                    settings.gamepadBindings[0][7] == std::vector<int>({14, -100}),
                "Default D-pad bindings must include matching left-stick directions.");
        require(settings.gamepadBindings[0][9].empty() &&
                    settings.gamepadBindings[0][12].empty() &&
                    settings.gamepadBindings[0][13].empty() &&
                    settings.gamepadBindings[0][14].empty() &&
                    settings.gamepadBindings[0][15].empty() &&
                    settings.keyboardBindings[0][9].empty() &&
                    settings.keyboardBindings[0][12].empty() &&
                    settings.keyboardBindings[0][13].empty() &&
                    settings.keyboardBindings[0][14].empty() &&
                    settings.keyboardBindings[0][15].empty(),
                "Extra, trigger, and stick-click controls must default to unbound.");
        settings.videoDriver = "direct3d12";
        settings.gpuPreference = "high_performance";
        settings.showFpsCounter = true;
        settings.useDetectedBios = false;
        settings.scaleMode = "linear";
        settings.shaderPreset = "crt";
        settings.fullscreenMode = "exclusive";
        settings.audioDriver = "default";
        settings.audioDevice = "Test speakers";
        settings.audioLatencyMs = 120;
        settings.audioGain = 0.75F;
        settings.audioMuted = true;
        settings.audioSync = false;
        settings.gamepadGuids[1] = "test-guid#1";
        settings.keyboardBindings[1][app::PadButtonCount - 1] = {77, 78};
        settings.gamepadBindings[1][1] = {2, 3};
        settings.gamepadBindings[1][12] = {-6};
        settings.gamepadBindings[1][4] = {-101, -103};
        settings.gamepadBindings[1][2].clear();
        settings.coreOptions["opera_region"] = "NTSC";
        settings.save(file);

        const app::Settings loaded = app::Settings::load(file);
        require(loaded.videoDriver == "direct3d12", "Direct3D 12 renderer did not persist.");
        require(loaded.gpuPreference == "high_performance", "GPU preference did not persist.");
        require(loaded.showFpsCounter, "FPS counter setting did not persist.");
        require(!loaded.useDetectedBios, "Optional BIOS setting did not persist.");
        require(loaded.scaleMode == "linear", "Scale mode did not persist.");
        require(loaded.shaderPreset == "crt", "Shader preset did not persist.");
        require(loaded.fullscreenMode == "exclusive", "Fullscreen mode did not persist.");
        require(loaded.audioDriver == "default", "Audio driver did not persist.");
        require(loaded.audioDevice == "Test speakers", "Audio output device did not persist.");
        require(loaded.audioLatencyMs == 120, "Audio latency did not persist.");
        require(loaded.audioGain == 0.75F, "Audio gain did not persist.");
        require(loaded.audioMuted, "Audio mute setting did not persist.");
        require(!loaded.audioSync, "Audio sync setting did not persist.");
        require(loaded.gamepadGuids[1] == "test-guid#1", "Gamepad selection did not persist.");
        require(loaded.keyboardBindings[1][app::PadButtonCount - 1] == std::vector<int>{77, 78},
                "Multiple keyboard mappings did not persist.");
        require(loaded.gamepadBindings[1][1] == std::vector<int>{2, 3},
                "Multiple gamepad button mappings did not persist.");
        require(loaded.gamepadBindings[1][12] == std::vector<int>{-6},
                "Gamepad trigger mapping did not persist.");
        require(loaded.gamepadBindings[1][4] == std::vector<int>{-101, -103},
                "Multiple directional stick mappings did not persist.");
        require(loaded.gamepadBindings[1][2].empty(), "Removed gamepad mappings did not persist.");
        require(loaded.coreOptions.at("opera_region") == "NTSC", "Core option did not persist.");

        {
            std::ofstream output(file, std::ios::trunc);
            output << "[video]\ndriver=direct3d11\n";
        }
        const app::Settings direct3d11 = app::Settings::load(file);
        require(direct3d11.videoDriver == "direct3d11", "Direct3D 11 renderer setting did not load.");

        {
            std::ofstream output(file, std::ios::trunc);
            output << "[input]\nplayer1_pad_4=-101\nplayer1_key_4=82,81\n";
        }
        const app::Settings migrated = app::Settings::load(file);
        require(migrated.gamepadBindings[0][4] == std::vector<int>{-101},
                "Legacy single-value gamepad mappings must remain compatible.");
        require(migrated.keyboardBindings[0][4] == std::vector<int>{82, 81},
                "Multiple mappings must load from the settings file.");

        {
            std::ofstream output(file, std::ios::trunc);
            output << "[video]\ndriver=unknown\ngpu=unknown\nfullscreen_mode=unknown\n"
                      "[audio]\ngain=nan\nmuted=invalid\nsync=invalid\n"
                      "latency_ms=999\n"
                      "[performance]\nfps_counter=invalid\n"
                      "[system]\nuse_detected_bios=invalid\n";
        }
        const app::Settings sanitized = app::Settings::load(file);
        require(sanitized.videoDriver == "opengl", "Unknown renderer must fall back to OpenGL.");
        require(sanitized.gpuPreference == "system", "Unknown GPU preference must fall back to system default.");
        require(sanitized.fullscreenMode == "borderless",
                "Unknown fullscreen mode must fall back to borderless windowed.");
        require(!sanitized.showFpsCounter, "Invalid FPS counter setting must fall back to disabled.");
        require(sanitized.useDetectedBios, "Invalid BIOS setting must fall back to enabled.");
        require(sanitized.audioGain == 1.0F, "Non-finite gain must fall back to its default.");
        require(!sanitized.audioMuted, "Invalid mute setting must fall back to unmuted.");
        require(sanitized.audioSync, "Invalid audio sync setting must fall back to enabled.");
        require(sanitized.audioLatencyMs == 200, "Audio latency must be clamped to the supported range.");
        {
            std::ofstream output(file, std::ios::trunc);
            output << "[audio]\ngain=2.0\n";
        }
        const app::Settings clippedGain = app::Settings::load(file);
        require(clippedGain.audioGain == 1.0F, "Audio gain above unity must be clamped to avoid clipping.");
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
