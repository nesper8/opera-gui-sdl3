#pragma once

#include "app/LibretroAbi.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace app {

struct CoreOption final {
    std::string key;
    std::string description;
    std::vector<std::string> values;
    std::string currentValue;
};

struct CoreCallbacks final {
    std::function<void(const void*, unsigned, unsigned, std::size_t)> video;
    std::function<void(unsigned)> pixelFormat;
    std::function<void(const std::int16_t*, std::size_t)> audio;
    std::function<void()> inputPoll;
    std::function<std::int16_t(unsigned, unsigned)> inputState;
    std::function<void()> optionsChanged;
    std::function<void(const std::string&)> error;
};

class LibretroCore final {
public:
    LibretroCore(std::filesystem::path systemDirectory,
                 std::filesystem::path saveDirectory,
                 CoreCallbacks callbacks);
    ~LibretroCore();

    LibretroCore(const LibretroCore&) = delete;
    LibretroCore& operator=(const LibretroCore&) = delete;

    void load(const std::filesystem::path& coreFile);
    void loadGame(const std::filesystem::path& gameFile);
    void run();
    void reset();
    void setOption(const std::string& key, const std::string& value);
    const std::vector<CoreOption>& options() const noexcept { return m_options; }
    const std::string& coreName() const noexcept { return m_coreName; }
    double frameRate() const noexcept { return m_frameRate; }
    double sampleRate() const noexcept { return m_sampleRate; }
    bool gameLoaded() const noexcept { return m_gameLoaded; }

private:
    using EnvironmentFn = void (*)(libretro::EnvironmentCallback);
    using VideoFn = void (*)(libretro::VideoRefreshCallback);
    using AudioFn = void (*)(libretro::AudioSampleCallback);
    using AudioBatchFn = void (*)(libretro::AudioSampleBatchCallback);
    using InputPollFn = void (*)(libretro::InputPollCallback);
    using InputStateFn = void (*)(libretro::InputStateCallback);

    template <typename Function>
    Function requiredSymbol(const char* name);
    static bool environment(unsigned command, void* data) noexcept;
    static void videoRefresh(const void* data, unsigned width, unsigned height, std::size_t pitch) noexcept;
    static void audioSample(std::int16_t left, std::int16_t right) noexcept;
    static std::size_t audioSampleBatch(const std::int16_t* data, std::size_t frames) noexcept;
    static void inputPoll() noexcept;
    static std::int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) noexcept;
    static void coreLog(int level, const char* format, ...) noexcept;
    void parseOptions(const libretro::Variable* variables);
    void parseCoreOptions(const libretro::CoreOptionDefinition* definitions);

    static LibretroCore* s_active;
    void* m_library{};
    bool m_initialized{};
    bool m_gameLoaded{};
    bool m_variableUpdated{};
    std::filesystem::path m_systemDirectory;
    std::filesystem::path m_saveDirectory;
    CoreCallbacks m_callbacks;
    std::string m_coreName;
    std::string m_coreVersion;
    double m_frameRate{};
    double m_sampleRate{};
    std::map<std::string, std::string> m_variables;
    std::vector<CoreOption> m_options;
    std::vector<std::int16_t> m_pendingAudio;

    EnvironmentFn m_setEnvironment{};
    VideoFn m_setVideoRefresh{};
    AudioFn m_setAudioSample{};
    AudioBatchFn m_setAudioSampleBatch{};
    InputPollFn m_setInputPoll{};
    InputStateFn m_setInputState{};
    void (*m_init)(){};
    void (*m_deinit)(){};
    unsigned (*m_apiVersion)(){};
    void (*m_getSystemInfo)(libretro::SystemInfo*){};
    void (*m_getSystemAvInfo)(libretro::SystemAvInfo*){};
    bool (*m_loadGame)(const libretro::GameInfo*){};
    void (*m_unloadGame)(){};
    void (*m_run)(){};
    void (*m_reset)(){};
    void (*m_setControllerPortDevice)(unsigned, unsigned){};
};

} // namespace app
