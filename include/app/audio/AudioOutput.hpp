#pragma once

#include "app/settings/Settings.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct AudioDeviceInfo final {
    SDL_AudioDeviceID id{};
    std::string name;
};

class AudioOutput final {
public:
    explicit AudioOutput(const Settings& settings) noexcept;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void configure(double sampleRate);
    void submit(const std::int16_t* samples, std::size_t frames) noexcept;
    void setGain(float gain) noexcept;
    static std::vector<AudioDeviceInfo> playbackDevices();

private:
    const Settings& m_settings;
    SDL_AudioStream* m_stream{};
};

} // namespace app
