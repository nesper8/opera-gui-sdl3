#pragma once

#include <cstddef>
#include <cstdint>

namespace app::libretro {

using EnvironmentCallback = bool (*)(unsigned, void*);
using VideoRefreshCallback = void (*)(const void*, unsigned, unsigned, std::size_t);
using AudioSampleCallback = void (*)(std::int16_t, std::int16_t);
using AudioSampleBatchCallback = std::size_t (*)(const std::int16_t*, std::size_t);
using InputPollCallback = void (*)();
using InputStateCallback = std::int16_t (*)(unsigned, unsigned, unsigned, unsigned);

struct GameInfo { const char* path; const void* data; std::size_t size; const char* meta; };
struct SystemInfo { const char* library_name; const char* library_version; const char* valid_extensions; bool need_fullpath; bool block_extract; };
struct GameGeometry { unsigned base_width; unsigned base_height; unsigned max_width; unsigned max_height; float aspect_ratio; };
struct SystemTiming { double fps; double sample_rate; };
struct SystemAvInfo { GameGeometry geometry; SystemTiming timing; };
struct Variable { const char* key; const char* value; };
struct CoreOptionValue { const char* value; const char* label; };
struct CoreOptionDefinition {
    const char* key;
    const char* description;
    const char* information;
    CoreOptionValue values[128];
    const char* defaultValue;
};
struct CoreOptionsIntl {
    CoreOptionDefinition* us;
    CoreOptionDefinition* local;
};
using LogPrintf = void (*)(int, const char*, ...);
struct LogCallback { LogPrintf log; };

enum Environment : unsigned {
    SetRotation = 1,
    GetOverscan = 2,
    GetCanDupe = 3,
    GetLogInterface = 27,
    SetMessage = 6,
    Shutdown = 7,
    SetPerformanceLevel = 8,
    GetSystemDirectoryPath = 9,
    SetPixelFormat = 10,
    SetInputDescriptors = 11,
    SetKeyboardCallback = 12,
    SetDiskControlInterface = 13,
    SetHardwareRender = 14,
    GetVariable = 15,
    SetVariables = 16,
    GetVariableUpdate = 17,
    SetSupportNoGame = 18,
    GetSaveDirectory = 31,
    GetCoreAssetsDirectory = 30,
    SetSupportAchievements = 42,
    GetLanguage = 39,
    SetCoreOptionsVersion = 52,
    SetCoreOptions = 53,
    SetCoreOptionsIntl = 54,
    GetInputBitmasks = 51 | 0x10000,
    GetInputMaxUsers = 61
};

enum PixelFormat : unsigned { Pixel0Rgb1555 = 0, PixelXrgb8888 = 1, PixelRgb565 = 2 };
inline constexpr unsigned DeviceJoypad = 1;
inline constexpr unsigned IdJoypadB = 0;
inline constexpr unsigned IdJoypadY = 1;
inline constexpr unsigned IdJoypadSelect = 2;
inline constexpr unsigned IdJoypadStart = 3;
inline constexpr unsigned IdJoypadUp = 4;
inline constexpr unsigned IdJoypadDown = 5;
inline constexpr unsigned IdJoypadLeft = 6;
inline constexpr unsigned IdJoypadRight = 7;
inline constexpr unsigned IdJoypadA = 8;
inline constexpr unsigned IdJoypadX = 9;
inline constexpr unsigned IdJoypadL = 10;
inline constexpr unsigned IdJoypadR = 11;
inline constexpr unsigned IdJoypadL2 = 12;
inline constexpr unsigned IdJoypadR2 = 13;
inline constexpr unsigned IdJoypadL3 = 14;
inline constexpr unsigned IdJoypadR3 = 15;
inline constexpr unsigned IdJoypadMask = 256;

} // namespace app::libretro
