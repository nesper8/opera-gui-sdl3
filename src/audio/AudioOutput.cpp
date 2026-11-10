#include "app/audio/AudioOutput.hpp"

#include "app/logging/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace app {

AudioOutput::AudioOutput(const Settings& settings) noexcept : m_settings(settings) {}

AudioOutput::~AudioOutput() {
    if (m_stream) SDL_DestroyAudioStream(m_stream);
}

void AudioOutput::configure(const double sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0) {
        throw std::runtime_error("The Opera core reported an unsupported audio sample rate.");
    }
    constexpr double deviceBufferMs = 10.0;
    const int deviceFrames = static_cast<int>(std::lround(sampleRate * deviceBufferMs / 1000.0));
    const std::string deviceFramesHint = std::to_string(std::max(deviceFrames, 1));
    if (!SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, deviceFramesHint.c_str())) {
        Logger::write(LogLevel::Warning, "SDL rejected the requested audio device buffer size.");
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(std::lround(sampleRate));
    SDL_AudioDeviceID deviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!m_settings.audioDevice.empty()) {
        const auto devices = playbackDevices();
        const auto selected = std::find_if(devices.begin(), devices.end(), [this](const AudioDeviceInfo& device) {
            return device.name == m_settings.audioDevice;
        });
        if (selected == devices.end()) {
            throw std::runtime_error("Selected audio output device is unavailable: " + m_settings.audioDevice);
        }
        deviceId = selected->id;
    }

    SDL_AudioStream* newStream = SDL_OpenAudioDeviceStream(deviceId, &spec, nullptr, nullptr);
    if (!newStream) throw std::runtime_error(std::string("Unable to open SDL audio output: ") + SDL_GetError());
    const float gain = m_settings.audioMuted ? 0.0F : std::clamp(m_settings.audioGain, 0.0F, 1.0F);
    if (!SDL_SetAudioStreamGain(newStream, gain)) {
        const std::string error = SDL_GetError();
        SDL_DestroyAudioStream(newStream);
        throw std::runtime_error("Unable to configure SDL audio output: " + error);
    }
    if (!SDL_ResumeAudioStreamDevice(newStream)) {
        const std::string error = SDL_GetError();
        SDL_DestroyAudioStream(newStream);
        throw std::runtime_error(std::string("Unable to start SDL audio output: ") + error);
    }
    if (m_stream) SDL_DestroyAudioStream(m_stream);
    m_stream = newStream;
    m_sampleRate = spec.freq;
    m_queueQueryFailed = false;
}

std::vector<AudioDeviceInfo> AudioOutput::playbackDevices() {
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    if (!ids && count == 0) {
        throw std::runtime_error(std::string("Unable to enumerate audio output devices: ") + SDL_GetError());
    }
    std::vector<AudioDeviceInfo> result;
    try {
        result.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const char* name = SDL_GetAudioDeviceName(ids[index]);
            if (name) result.push_back({ids[index], name});
        }
    } catch (...) {
        SDL_free(ids);
        throw;
    }
    SDL_free(ids);
    return result;
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

void AudioOutput::discardQueuedAudio() noexcept {
    if (m_stream && !SDL_ClearAudioStream(m_stream)) {
        Logger::write(LogLevel::Error, std::string("Unable to clear queued audio: ") + SDL_GetError());
    }
}

void AudioOutput::setGain(const float gain) noexcept {
    const float effectiveGain = m_settings.audioMuted ? 0.0F : std::clamp(gain, 0.0F, 1.0F);
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, effectiveGain)) {
        Logger::write(LogLevel::Error, std::string("Unable to set audio gain: ") + SDL_GetError());
    }
}

void AudioOutput::setMuted(const bool muted) noexcept {
    const float effectiveGain = muted ? 0.0F : std::clamp(m_settings.audioGain, 0.0F, 1.0F);
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, effectiveGain)) {
        Logger::write(LogLevel::Error, std::string("Unable to set audio mute state: ") + SDL_GetError());
    }
}

std::optional<double> AudioOutput::queuedMilliseconds() noexcept {
    if (!m_stream || m_sampleRate <= 0) return 0.0;
    const int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes < 0) {
        if (!m_queueQueryFailed) {
            Logger::write(LogLevel::Error, std::string("Unable to query queued audio: ") + SDL_GetError());
            m_queueQueryFailed = true;
        }
        return std::nullopt;
    }
    m_queueQueryFailed = false;
    constexpr double bytesPerFrame = 2.0 * sizeof(std::int16_t);
    return static_cast<double>(queuedBytes) * 1000.0 /
           (bytesPerFrame * static_cast<double>(m_sampleRate));
}

} // namespace app
