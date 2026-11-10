#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace app {

inline constexpr std::size_t PadButtonCount = 16;

struct Settings final {
    std::string videoDriver = "opengl";
    std::string gpuPreference = "system";
    std::string scaleMode = "nearest";
    std::string shaderPreset = "none";
    std::string fullscreenMode = "borderless";
    bool vsync = true;
    bool fullscreen = false;
    bool showFpsCounter = false;
    bool useDetectedBios = true;
    std::string audioDriver = "default";
    std::string audioDevice;
    int audioLatencyMs = 40;
    float audioGain = 1.0F;
    bool audioMuted = false;
    bool audioSync = true;
    std::array<std::string, 2> gamepadGuids{};
    std::array<std::array<std::vector<int>, PadButtonCount>, 2> keyboardBindings{};
    std::array<std::array<std::vector<int>, PadButtonCount>, 2> gamepadBindings{};
    std::map<std::string, std::string> coreOptions;

    static Settings defaults();
    static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace app
