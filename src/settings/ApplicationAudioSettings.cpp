#include "app/application/Application.hpp"
#include "app/audio/AudioOutput.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/settings/SettingsWidgets.hpp"

#include <imgui.h>

#include <exception>
#include <string>
namespace app {
void Application::drawAudioSettings() {
    if (ImGui::Button("Revert audio settings to defaults")) {
        const Settings defaults = Settings::defaults();
        const std::string previousDriver = m_settings.audioDriver;
        const std::string previousDevice = m_settings.audioDevice;
        const int previousLatency = m_settings.audioLatencyMs;
        const float previousGain = m_settings.audioGain;
        m_settings.audioDriver = defaults.audioDriver;
        m_settings.audioDevice = defaults.audioDevice;
        m_settings.audioLatencyMs = defaults.audioLatencyMs;
        m_settings.audioGain = defaults.audioGain;
        try {
            if (m_session.core() && m_session.core()->gameLoaded()) {
                m_session.audio()->configure(m_session.core()->sampleRate());
            } else {
                m_session.audio()->setGain(m_settings.audioGain);
            }
            saveSettings();
            if (m_error.empty()) {
                m_status = "Audio settings reverted. Restart to apply the audio driver default.";
            }
        } catch (const std::exception& error) {
            m_settings.audioDriver = previousDriver;
            m_settings.audioDevice = previousDevice;
            m_settings.audioLatencyMs = previousLatency;
            m_settings.audioGain = previousGain;
            setError(std::string("Unable to revert audio settings: ") + error.what());
        }
    }
    drawSaveSettingsButton();
    ImGui::SameLine();
    ImGui::TextDisabled("Audio settings only.");

    const bool driverChanged = ui::selectableCombo("Audio driver", m_settings.audioDriver,
        {{"wasapi", "WASAPI"}, {"default", "SDL default"}});
    bool outputChanged = false;
    const std::string previousDevice = m_settings.audioDevice;
    {
        const char* selectedDevice = m_settings.audioDevice.empty()
            ? "System default" : m_settings.audioDevice.c_str();
        if (ImGui::BeginCombo("Output device", selectedDevice)) {
            if (ImGui::Selectable("System default", m_settings.audioDevice.empty())) {
                m_settings.audioDevice.clear();
                outputChanged = true;
            }
            for (const AudioDeviceInfo& device : m_audioDevices) {
                const bool selected = m_settings.audioDevice == device.name;
                if (ImGui::Selectable(device.name.c_str(), selected)) {
                    m_settings.audioDevice = device.name;
                    outputChanged = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    const int previousLatency = m_settings.audioLatencyMs;
    const bool latencyChanged = ImGui::SliderInt(
        "Audio latency (ms)", &m_settings.audioLatencyMs, 16, 200);
    if (latencyChanged) {
        ImGui::SameLine();
        ImGui::TextDisabled("(lower latency, higher stutter risk)");
    }

    if (ImGui::SliderFloat("Output gain", &m_settings.audioGain, 0.0F, 1.0F, "%.2f")) {
        if (m_session.audio()) m_session.audio()->setGain(m_settings.audioGain);
        saveSettings();
    }

    if (outputChanged || latencyChanged) {
        if (m_session.core() && m_session.core()->gameLoaded()) {
            try {
                m_session.audio()->configure(m_session.core()->sampleRate());
            } catch (const std::exception& error) {
                m_settings.audioDevice = previousDevice;
                m_settings.audioLatencyMs = previousLatency;
                setError(std::string("Unable to reconfigure audio output: ") + error.what());
            }
        }
        saveSettings();
        if (m_error.empty()) m_status = "Audio output settings applied.";
    }
    if (driverChanged) {
        saveSettings();
        m_status = "Restart the application to apply the audio driver change.";
    }
    ImGui::TextWrapped("Playback rate is kept at the core's native speed. Output gain is capped at unity to avoid introducing clipping.");
    ImGui::TextWrapped("The requested device buffer size is a hint; SDL and the audio driver may choose a different effective latency.");
}

} // namespace app
