#include "app/AudioOutput.hpp"

#include "app/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace app {

AudioOutput::AudioOutput(const Settings& settings) noexcept : m_settings(settings) {}

AudioOutput::~AudioOutput() {
    if (m_stream) SDL_DestroyAudioStream(m_stream);
}

void AudioOutput::configure(const double sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0) {
        throw std::runtime_error("The Opera core reported an unsupported audio sample rate.");
    }
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(std::lround(sampleRate));
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_stream) throw std::runtime_error(std::string("Unable to open SDL audio output: ") + SDL_GetError());
    setGain(m_settings.audioGain);
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
        throw std::runtime_error(std::string("Unable to start SDL audio output: ") + SDL_GetError());
    }
}

void AudioOutput::submit(const std::int16_t* samples, const std::size_t frames) noexcept {
    if (!m_stream || !samples || frames == 0) return;
    constexpr std::size_t bytesPerFrame = 2 * sizeof(std::int16_t);
    if (frames > static_cast<std::size_t>(std::numeric_limits<int>::max()) / bytesPerFrame) {
        Logger::write(LogLevel::Error, "Opera core audio batch exceeds SDL's supported buffer size.");
        return;
    }
    if (!SDL_PutAudioStreamData(m_stream, samples, static_cast<int>(frames * bytesPerFrame))) {
        Logger::write(LogLevel::Error, std::string("Unable to queue audio: ") + SDL_GetError());
    }
}

void AudioOutput::setGain(const float gain) noexcept {
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, std::clamp(gain, 0.0F, 2.0F))) {
        Logger::write(LogLevel::Error, std::string("Unable to set audio gain: ") + SDL_GetError());
    }
}

} // namespace app
