#include "app/application/Application.hpp"
#include "app/core/LibretroCore.hpp"

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <string>
#include <vector>
namespace app {
void Application::drawPerformanceSettings() {
    if (ImGui::Button("Revert performance settings to defaults")) {
        m_settings.showFpsCounter = Settings::defaults().showFpsCounter;
        saveSettings();
        if (m_error.empty()) m_status = "Performance settings reverted to defaults.";
    }
    drawSaveSettingsButton();
    if (ImGui::Checkbox("Show FPS counter (F3)", &m_settings.showFpsCounter)) {
        saveSettings();
    }
    ImGui::TextWrapped("The counter reports the frontend's rendered frames per second.");
    ImGui::TextUnformatted("F3 toggles the counter at any time.");
}

void Application::drawCoreSettings() {
    const std::string systemDirectory = pathToUtf8(m_systemDirectory);
    const std::string biosDirectory = pathToUtf8(m_biosDirectory);
    const std::string saveDirectory = pathToUtf8(m_saveDirectory);
    ImGui::TextWrapped("Optional BIOS directory: %s", biosDirectory.c_str());
    if (m_detectedBios) {
        const std::string biosName = pathToUtf8(m_detectedBios->filename());
        ImGui::TextWrapped("BIOS file found: %s", biosName.c_str());
    } else {
        ImGui::TextUnformatted("BIOS file found: none; using the user system directory.");
    }
    if (ImGui::Checkbox("Use detected BIOS when available", &m_settings.useDetectedBios)) {
        saveSettings();
        m_status = "Restart the application to apply the BIOS setting.";
    }
    ImGui::TextWrapped("System files: %s", systemDirectory.c_str());
    ImGui::TextWrapped("Save files: %s", saveDirectory.c_str());
    if (!m_session.core()) {
        ImGui::TextWrapped("Install opera_libretro.dll to configure the Opera core.");
        return;
    }
    ImGui::Text("%s", m_coreName.c_str());
    if (!m_session.core()->gameLoaded()) ImGui::TextUnformatted("Load a game to begin emulation.");
    if (ImGui::Button("Revert to defaults")) {
        try {
            m_session.core()->resetOptionsToDefaults();
            m_settings.coreOptions.clear();
            for (const CoreOption& option : m_session.core()->options()) {
                m_settings.coreOptions.emplace(option.key, option.currentValue);
            }
            saveSettings();
            if (m_error.empty()) m_status = "Opera core options reverted to defaults.";
        } catch (const std::exception& error) {
            setError(std::string("Unable to revert Opera core options: ") + error.what());
        }
    }
    drawSaveSettingsButton();
    ImGui::SameLine();
    ImGui::TextDisabled("Resets Opera core options only.");
    for (const auto& option : m_session.core()->options()) {
        if (option.values.empty()) continue;
        int selected = 0;
        const auto current = std::find(option.values.begin(), option.values.end(), option.currentValue);
        if (current != option.values.end()) selected = static_cast<int>(std::distance(option.values.begin(), current));
        std::vector<const char*> labels;
        labels.reserve(option.values.size());
        for (const auto& value : option.values) labels.push_back(value.c_str());
        ImGui::PushID(option.key.c_str());
        if (ImGui::Combo(option.description.c_str(), &selected, labels.data(), static_cast<int>(labels.size()))) {
            try {
                m_session.core()->setOption(option.key, option.values[static_cast<std::size_t>(selected)]);
                m_settings.coreOptions[option.key] = option.values[static_cast<std::size_t>(selected)];
                saveSettings();
            } catch (const std::exception& error) {
                setError(error.what());
            }
        }
        ImGui::PopID();
    }
}

} // namespace app
