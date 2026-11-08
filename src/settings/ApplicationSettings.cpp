#include "app/application/Application.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/input/InputManager.hpp"

#include <imgui.h>

#include <string>
namespace app {
void Application::drawInterface() {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(510, 690), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Opera - 3DO", &m_uiVisible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", m_coreName.empty() ? "Opera core is not loaded." : m_coreName.c_str());
    if (!m_gamePath.empty()) ImGui::TextWrapped("Game: %s", m_gamePath.c_str());

    if (ImGui::Button("Open game...")) openGameDialog();
    ImGui::SameLine();
    if (ImGui::Button("Download Opera core")) requestCoreDownload();
    if (m_session.core() && m_session.core()->gameLoaded()) {
        ImGui::SameLine();
        if (ImGui::Button("Reset (F1)")) m_session.core()->reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide menu (F2)")) {
        m_uiVisible = false;
        m_lastMouseActivity = std::chrono::steady_clock::now();
        m_captureBinding.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit")) m_running = false;
    ImGui::Separator();

    if (ImGui::BeginTabBar("settings-tabs")) {
        if (ImGui::BeginTabItem("Input")) {
            drawInputSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Video")) {
            drawVideoSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            drawAudioSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Performance")) {
            drawPerformanceSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Core")) {
            drawCoreSettings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", m_status.c_str());
    if (!m_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.35F, 0.3F, 1.0F));
        ImGui::TextWrapped("%s", m_error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace app
