#include "app/settings/Settings.hpp"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace app {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int parseInteger(const std::string& value, const int fallback) {
    int result = fallback;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end ? result : fallback;
}

float parseGain(const std::string& value, const float fallback) {
    try {
        const float result = std::stof(value);
        return std::isfinite(result) ? result : fallback;
    } catch (const std::exception&) {
        return fallback;
    }
}

bool parseBool(const std::string& value, const bool fallback) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    return fallback;
}

std::vector<int> parseIntegerList(const std::string& value, const int fallback) {
    std::vector<int> result;
    if (value.empty()) return result;

    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t separator = value.find(',', start);
        const std::size_t end = separator == std::string::npos ? value.size() : separator;
        result.push_back(parseInteger(value.substr(start, end - start), fallback));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return result;
}

} // namespace

Settings Settings::defaults() {
    Settings result;
    constexpr std::array<int, PadButtonCount> defaultKeys{
        27, 29, 42, 40, 82, 81, 80, 79, 4, 0, 20, 26, 0, 0, 0, 0
    };
    const std::array<std::vector<int>, PadButtonCount> defaultGamepad{
        std::vector<int>{1}, {0}, {4}, {6},
        {11, -103}, {12, -102}, {13, -101}, {14, -100},
        {3}, {}, {9}, {10}, {}, {}, {}, {}
    };
    for (std::size_t button = 0; button < PadButtonCount; ++button) {
        for (std::size_t player = 0; player < result.keyboardBindings.size(); ++player) {
            if (defaultKeys[button] != 0) {
                result.keyboardBindings[player][button].push_back(defaultKeys[button]);
            }
            result.gamepadBindings[player][button] = defaultGamepad[button];
        }
    }
    return result;
}

Settings Settings::load(const std::filesystem::path& file) {
    Settings result = defaults();
    if (!std::filesystem::exists(file)) return result;

    std::ifstream input(file);
    if (!input) throw std::runtime_error("Unable to read settings file: " + file.string());

    std::string section;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string text = trim(line);
        if (text.empty() || text.starts_with('#') || text.starts_with(';')) continue;
        if (text.front() == '[' && text.back() == ']') {
            section = text.substr(1, text.size() - 2);
            continue;
        }
        const auto separator = text.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("Invalid settings entry on line " + std::to_string(lineNumber));
        }
        const std::string key = trim(text.substr(0, separator));
        const std::string value = trim(text.substr(separator + 1));
        if (key.empty()) {
            throw std::runtime_error("Empty settings key on line " + std::to_string(lineNumber));
        }

        if (section == "video") {
            if (key == "driver" && (value == "opengl" || value == "vulkan" ||
                                    value == "direct3d11" || value == "direct3d12")) {
                result.videoDriver = value;
            }
            else if (key == "gpu" && (value == "system" || value == "power_saving" ||
                                      value == "high_performance")) result.gpuPreference = value;
            else if (key == "scale" && (value == "nearest" || value == "linear")) result.scaleMode = value;
            else if (key == "shader" && (value == "none" || value == "crt")) result.shaderPreset = value;
            else if (key == "fullscreen_mode" &&
                     (value == "borderless" || value == "exclusive")) result.fullscreenMode = value;
            else if (key == "vsync") result.vsync = parseBool(value, result.vsync);
            else if (key == "fullscreen") result.fullscreen = parseBool(value, result.fullscreen);
        } else if (section == "audio") {
            if (key == "driver" && (value == "wasapi" || value == "default")) result.audioDriver = value;
            else if (key == "device") result.audioDevice = value;
            else if (key == "latency_ms") {
                result.audioLatencyMs = std::clamp(parseInteger(value, result.audioLatencyMs), 16, 200);
            } else if (key == "gain") result.audioGain = std::clamp(parseGain(value, result.audioGain), 0.0F, 1.0F);
            else if (key == "muted") result.audioMuted = parseBool(value, result.audioMuted);
            else if (key == "sync") result.audioSync = parseBool(value, result.audioSync);
        } else if (section == "performance") {
            if (key == "fps_counter") result.showFpsCounter = parseBool(value, result.showFpsCounter);
        } else if (section == "system") {
            if (key == "use_detected_bios") {
                result.useDetectedBios = parseBool(value, result.useDetectedBios);
            }
        } else if (section == "input") {
            if (key == "player1_guid") result.gamepadGuids[0] = value;
            else if (key == "player2_guid") result.gamepadGuids[1] = value;
            else if (key.starts_with("player1_key_") || key.starts_with("player2_key_") ||
                     key.starts_with("player1_pad_") || key.starts_with("player2_pad_")) {
                const std::size_t player = key.starts_with("player1_") ? 0U : 1U;
                const bool gamepad = key.starts_with("player1_pad_") || key.starts_with("player2_pad_");
                const auto buttonText = std::string_view(key).substr(12);
                const int button = parseInteger(std::string(buttonText), -1);
                if (button >= 0 && static_cast<std::size_t>(button) < PadButtonCount) {
                    if (gamepad) {
                        result.gamepadBindings[player][static_cast<std::size_t>(button)] =
                            parseIntegerList(value, -1);
                    } else {
                        result.keyboardBindings[player][static_cast<std::size_t>(button)] =
                            parseIntegerList(value, 0);
                    }
                }
            }
        } else if (section == "core") {
            result.coreOptions.insert_or_assign(key, value);
        }
    }
    if (!input.eof()) throw std::runtime_error("Error while reading settings file: " + file.string());
    return result;
}

void Settings::save(const std::filesystem::path& file) const {
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path());
    auto temporary = file;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::out | std::ios::trunc);
        if (!output) throw std::runtime_error("Unable to write settings file: " + temporary.string());
        output << "[video]\n"
               << "driver=" << videoDriver << '\n'
               << "gpu=" << gpuPreference << '\n'
               << "scale=" << scaleMode << '\n'
               << "shader=" << shaderPreset << '\n'
               << "fullscreen_mode=" << fullscreenMode << '\n'
               << "vsync=" << (vsync ? "true" : "false") << '\n'
               << "fullscreen=" << (fullscreen ? "true" : "false") << "\n\n"
               << "[audio]\n"
               << "driver=" << audioDriver << '\n'
               << "device=" << audioDevice << '\n'
               << "latency_ms=" << audioLatencyMs << '\n'
               << "gain=" << audioGain << '\n'
               << "muted=" << (audioMuted ? "true" : "false") << '\n'
               << "sync=" << (audioSync ? "true" : "false") << "\n\n"
               << "[performance]\n"
               << "fps_counter=" << (showFpsCounter ? "true" : "false") << "\n\n"
               << "[system]\n"
               << "use_detected_bios=" << (useDetectedBios ? "true" : "false") << "\n\n"
               << "[input]\n"
               << "player1_guid=" << gamepadGuids[0] << '\n'
               << "player2_guid=" << gamepadGuids[1] << '\n';
        for (std::size_t player = 0; player < keyboardBindings.size(); ++player) {
            for (std::size_t button = 0; button < PadButtonCount; ++button) {
                output << "player" << (player + 1) << "_key_" << button << '=';
                for (std::size_t index = 0; index < keyboardBindings[player][button].size(); ++index) {
                    if (index != 0) output << ',';
                    output << keyboardBindings[player][button][index];
                }
                output << '\n' << "player" << (player + 1) << "_pad_" << button << '=';
                for (std::size_t index = 0; index < gamepadBindings[player][button].size(); ++index) {
                    if (index != 0) output << ',';
                    output << gamepadBindings[player][button][index];
                }
                output << '\n';
            }
        }
        output << "\n[core]\n";
        for (const auto& [key, value] : coreOptions) output << key << '=' << value << '\n';
        output.flush();
        if (!output) throw std::runtime_error("Failed while writing settings file: " + temporary.string());
    }

    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::filesystem::filesystem_error("Unable to replace settings file", temporary, file,
                                                std::error_code(static_cast<int>(GetLastError()),
                                                                std::system_category()));
    }
}

} // namespace app
