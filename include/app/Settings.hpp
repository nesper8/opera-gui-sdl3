#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>

namespace app {

inline constexpr std::size_t PadButtonCount = 16;

struct Settings final {
    std::string videoDriver = "opengl";
    std::string scaleMode = "nearest";
    std::string shaderPreset = "none";
    bool vsync = true;
    bool fullscreen = false;
    std::string audioDriver = "wasapi";
    float audioGain = 1.0F;
    std::array<std::string, 2> gamepadGuids{};
    std::array<std::array<int, PadButtonCount>, 2> keyboardBindings{};
    std::map<std::string, std::string> coreOptions;

    static Settings defaults();
    static Settings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

} // namespace app
