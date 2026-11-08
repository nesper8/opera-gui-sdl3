#pragma once

#include "app/Settings.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>

namespace app {

class AudioOutput final {
public:
    explicit AudioOutput(const Settings& settings) noexcept;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void configure(double sampleRate);
    void submit(const std::int16_t* samples, std::size_t frames) noexcept;
    void setGain(float gain) noexcept;

private:
    const Settings& m_settings;
    SDL_AudioStream* m_stream{};
};

} // namespace app
