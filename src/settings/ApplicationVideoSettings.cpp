#include "app/application/Application.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/logging/Logger.hpp"
#include "app/settings/SettingsWidgets.hpp"
#include "app/platform/WindowsGpuPreference.hpp"

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
namespace app {
void Application::drawVideoSettings() {
    const CoreOption* highResolutionOption = nullptr;
    if (m_session.core()) {
        const auto option = std::find_if(m_session.core()->options().begin(), m_session.core()->options().end(),
            [](const CoreOption& item) { return item.key == "opera_high_resolution"; });
        if (option != m_session.core()->options().end()) highResolutionOption = &*option;
    }

    if (ImGui::Button("Revert video settings to defaults")) {
        const Settings defaults = Settings::defaults();
        const Settings previousSettings = m_settings;
        const std::string previousCoreResolution = highResolutionOption
            ? highResolutionOption->currentValue : std::string{};
        try {
            setWindowsGpuPreference(defaults.gpuPreference);
            m_settings.videoDriver = defaults.videoDriver;
            m_settings.gpuPreference = defaults.gpuPreference;
            m_settings.scaleMode = defaults.scaleMode;
            m_settings.shaderPreset = defaults.shaderPreset;
            m_settings.fullscreenMode = defaults.fullscreenMode;
            m_settings.vsync = defaults.vsync;
            m_settings.fullscreen = defaults.fullscreen;
            m_pendingFullscreen = m_settings.fullscreen;
            if (m_frameTexture) {
                SDL_SetTextureScaleMode(m_frameTexture, SDL_SCALEMODE_NEAREST);
            }
            if (!m_runtime.setVsync(m_settings.vsync)) {
                m_settings.vsync = false;
                throw std::runtime_error(std::string("Unable to restore VSync: ") + SDL_GetError());
            }
            if (highResolutionOption) {
                m_session.core()->setOption(highResolutionOption->key, highResolutionOption->defaultValue);
                m_settings.coreOptions[highResolutionOption->key] = highResolutionOption->defaultValue;
            }
            saveSettings();
            if (m_error.empty()) {
                m_status = "Video settings reverted. Restart to apply the video driver and GPU defaults.";
            }
        } catch (const std::exception& error) {
            m_settings = previousSettings;
            if (highResolutionOption && highResolutionOption->currentValue != previousCoreResolution) {
                try {
                    m_session.core()->setOption(highResolutionOption->key, previousCoreResolution);
                } catch (const std::exception& restoreError) {
                    Logger::write(LogLevel::Error,
                        std::string("Unable to restore the previous internal resolution: ") +
                        restoreError.what());
                }
            }
            m_pendingFullscreen = previousSettings.fullscreen;
            if (!m_runtime.setVsync(previousSettings.vsync)) {
                Logger::write(LogLevel::Error,
                    std::string("Unable to restore previous VSync: ") + SDL_GetError());
            }
            if (m_frameTexture) {
                SDL_SetTextureScaleMode(m_frameTexture,
                    previousSettings.scaleMode == "nearest" ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
            }
            try {
                setWindowsGpuPreference(previousSettings.gpuPreference);
            } catch (const std::exception& restoreError) {
                Logger::write(LogLevel::Error,
                    std::string("Unable to restore the previous Windows GPU preference: ") +
                    restoreError.what());
            }
            setError(std::string("Unable to revert video settings: ") + error.what());
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Video settings only.");

    bool changed = ui::selectableCombo("Video driver", m_settings.videoDriver,
        {{"opengl", "OpenGL"}, {"vulkan", "Vulkan"}});
    const std::string previousGpuPreference = m_settings.gpuPreference;
    const bool gpuPreferenceChanged = ui::selectableCombo("GPU preference", m_settings.gpuPreference,
        {{"system", "System default"}, {"power_saving", "Power saving GPU"},
         {"high_performance", "High performance GPU"}});
    if (gpuPreferenceChanged) {
        try {
            setWindowsGpuPreference(m_settings.gpuPreference);
            saveSettings();
            m_status = "GPU preference saved. Restart the application for it to take effect.";
        } catch (const std::exception& error) {
            m_settings.gpuPreference = previousGpuPreference;
            setError(std::string("Unable to apply Windows GPU preference: ") + error.what());
        }
    }
    changed |= ui::selectableCombo("Scaling", m_settings.scaleMode,
        {{"nearest", "Nearest (pixel sharp)"}, {"linear", "Linear"}});
    changed |= ui::selectableCombo("Shader preset", m_settings.shaderPreset,
        {{"none", "None"}, {"crt", "CRT scanlines"}});
    const char* fullscreenModeLabel = m_settings.fullscreenMode == "exclusive"
        ? "Exclusive fullscreen" : "Borderless windowed";
    bool fullscreenModeChanged = false;
    if (ImGui::BeginCombo("Fullscreen mode", fullscreenModeLabel)) {
        const bool borderlessSelected = m_settings.fullscreenMode == "borderless";
        if (ImGui::Selectable("Borderless windowed", borderlessSelected)) {
            m_settings.fullscreenMode = "borderless";
            fullscreenModeChanged = true;
        }
        if (borderlessSelected) ImGui::SetItemDefaultFocus();
        const bool exclusiveSelected = m_settings.fullscreenMode == "exclusive";
        if (ImGui::Selectable("Exclusive fullscreen", exclusiveSelected)) {
            m_settings.fullscreenMode = "exclusive";
            fullscreenModeChanged = true;
        }
        if (exclusiveSelected) ImGui::SetItemDefaultFocus();
        ImGui::EndCombo();
    }
    if (fullscreenModeChanged && m_settings.fullscreen) m_pendingFullscreen = true;
    changed |= fullscreenModeChanged;
    changed |= ImGui::Checkbox("Vertical sync", &m_settings.vsync);

    if (highResolutionOption &&
        std::find(highResolutionOption->values.begin(), highResolutionOption->values.end(), "disabled") !=
            highResolutionOption->values.end() &&
        std::find(highResolutionOption->values.begin(), highResolutionOption->values.end(), "enabled") !=
            highResolutionOption->values.end()) {
        bool highResolution = highResolutionOption->currentValue == "enabled";
        if (ImGui::Checkbox("High resolution (640x480)", &highResolution)) {
            const std::string previousValue = highResolutionOption->currentValue;
            const std::string value = highResolution ? "enabled" : "disabled";
            try {
                m_session.core()->setOption(highResolutionOption->key, value);
                m_settings.coreOptions[highResolutionOption->key] = value;
                saveSettings();
                m_status = highResolution
                    ? "Opera high-resolution mode enabled."
                    : "Opera high-resolution mode disabled.";
            } catch (const std::exception& error) {
                try {
                    m_session.core()->setOption(highResolutionOption->key, previousValue);
                } catch (const std::exception& restoreError) {
                    Logger::write(LogLevel::Error,
                        std::string("Unable to restore the previous internal resolution: ") +
                        restoreError.what());
                }
                m_settings.coreOptions[highResolutionOption->key] = previousValue;
                setError(std::string("Unable to change Opera internal resolution: ") + error.what());
            }
        }
    } else if (!highResolutionOption) {
        ImGui::TextDisabled("Load the Opera core to configure its internal resolution.");
    }

    if (ImGui::Checkbox("Fullscreen (F11)", &m_settings.fullscreen)) {
        m_pendingFullscreen = m_settings.fullscreen;
        changed = true;
    }
    ImGui::TextDisabled("F11 toggles between windowed and the selected fullscreen mode.");
    if (changed) {
        saveSettings();
        m_status = fullscreenModeChanged
            ? (m_settings.fullscreen ? "Fullscreen mode updated." : "Fullscreen mode saved.")
            : "Restart the application to apply a video driver change.";
        if (m_frameTexture) SDL_SetTextureScaleMode(m_frameTexture,
            m_settings.scaleMode == "nearest" ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        if (!m_runtime.setVsync(m_settings.vsync)) {
            m_settings.vsync = false;
            setError(std::string("VSync change failed: ") + SDL_GetError());
            saveSettings();
        }
    }
    ImGui::TextWrapped("OpenGL or Vulkan is selected through SDL3's renderer backend. CRT scanlines are applied at presentation.");
    ImGui::TextWrapped("GPU preference is stored in Windows per-app graphics settings and requires a restart. Windows or vendor driver settings may override it.");
}

} // namespace app
