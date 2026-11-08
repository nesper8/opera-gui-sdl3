#include "app/AudioOutput.hpp"

#include "app/Logger.hpp"

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
    const int deviceFrames = static_cast<int>(std::lround(
        sampleRate * static_cast<double>(m_settings.audioLatencyMs) / 1000.0));
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
    if (!SDL_SetAudioStreamGain(newStream, std::clamp(m_settings.audioGain, 0.0F, 2.0F)) ||
        !SDL_SetAudioStreamFrequencyRatio(newStream, 1.0F)) {
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
    m_sampleRate = sampleRate;
    m_frequencyRatio = 1.0F;
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
        return;
    }
    updateRateControl();
}

void AudioOutput::updateRateControl() noexcept {
    if (!m_stream || !m_settings.audioRateControl || m_sampleRate <= 0.0) return;
    const int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes < 0) {
        Logger::write(LogLevel::Error, std::string("Unable to inspect queued audio: ") + SDL_GetError());
        return;
    }
    constexpr double BytesPerFrame = 2.0 * sizeof(std::int16_t);
    const double targetFrames = m_sampleRate * static_cast<double>(m_settings.audioLatencyMs) / 1000.0;
    const double queueError = (static_cast<double>(queuedBytes) / BytesPerFrame - targetFrames) /
                              std::max(targetFrames, 1.0);
    const float targetRatio = static_cast<float>(std::clamp(1.0 + queueError * 0.05, 0.95, 1.05));
    const float nextRatio = m_frequencyRatio + (targetRatio - m_frequencyRatio) * 0.15F;
    if (std::abs(nextRatio - m_frequencyRatio) < 0.0001F) return;
    if (!SDL_SetAudioStreamFrequencyRatio(m_stream, nextRatio)) {
        Logger::write(LogLevel::Error, std::string("Unable to adjust audio rate: ") + SDL_GetError());
        return;
    }
    m_frequencyRatio = nextRatio;
}

void AudioOutput::setRateControl(const bool enabled) noexcept {
    if (!m_stream) return;
    if (!SDL_SetAudioStreamFrequencyRatio(m_stream, 1.0F)) {
        Logger::write(LogLevel::Error, std::string("Unable to reset audio playback rate: ") + SDL_GetError());
        return;
    }
    m_frequencyRatio = 1.0F;
    if (enabled) updateRateControl();
}

void AudioOutput::setGain(const float gain) noexcept {
    if (m_stream && !SDL_SetAudioStreamGain(m_stream, std::clamp(gain, 0.0F, 2.0F))) {
        Logger::write(LogLevel::Error, std::string("Unable to set audio gain: ") + SDL_GetError());
    }
}

} // namespace app
