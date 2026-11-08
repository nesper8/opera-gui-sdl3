#include "app/application/Application.hpp"
#include "app/input/InputManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <exception>
#include <string>
namespace app {
namespace {
const char* playerName(const std::size_t player) {
    return player == 0 ? "Player 1" : "Player 2";
}
} // namespace

const std::array<Application::PadControl, PadButtonCount>& Application::padControls() {
    static constexpr std::array<PadControl, PadButtonCount> controls{
        PadControl{"D-pad Up", 4}, {"D-pad Down", 5}, {"D-pad Left", 6}, {"D-pad Right", 7},
        {"A", 1}, {"B", 0}, {"C", 8}, {"L", 10}, {"R", 11},
        {"Select (X)", 2}, {"Start (P)", 3}, {"Extra X", 9},
        {"L2", 12}, {"R2", 13}, {"L3", 14}, {"R3", 15}
    };
    return controls;
}



void Application::drawInputSettings() {
    if (ImGui::Button("Revert input settings to defaults")) {
        const Settings defaults = Settings::defaults();
        m_settings.keyboardBindings = defaults.keyboardBindings;
        m_settings.gamepadBindings = defaults.gamepadBindings;
        m_settings.gamepadGuids = defaults.gamepadGuids;
        m_captureBinding.reset();
        try {
            m_session.input()->refresh();
            saveSettings();
            if (m_error.empty()) m_status = "Input settings reverted to defaults.";
        } catch (const std::exception& error) {
            setError(std::string("Unable to reset input settings: ") + error.what());
        }
    }

    if (ImGui::BeginCombo("Player", playerName(m_selectedPlayer))) {
        for (std::size_t player = 0; player < 2; ++player) {
            const bool selected = m_selectedPlayer == player;
            if (ImGui::Selectable(playerName(player), selected)) m_selectedPlayer = player;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    std::string selected = m_settings.gamepadGuids[m_selectedPlayer];
    const auto devices = m_session.input()->gamepads();
    const auto gamepadLabel = [&devices](const std::size_t deviceIndex) {
        const auto& device = devices[deviceIndex];
        std::size_t matchingCount = 0;
        std::size_t matchingIndex = 0;
        for (std::size_t index = 0; index < devices.size(); ++index) {
            if (devices[index].name != device.name) continue;
            if (index < deviceIndex) ++matchingIndex;
            ++matchingCount;
        }
        return matchingCount > 1
            ? device.name + " (" + std::to_string(matchingIndex + 1) + ")"
            : device.name;
    };
    std::string selectedLabel = selected.empty() ? "Keyboard" : "Disconnected controller";
    for (std::size_t index = 0; index < devices.size(); ++index) {
        if (devices[index].key == selected) {
            selectedLabel = gamepadLabel(index);
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo("Device", selectedLabel.c_str())) {
        if (ImGui::Selectable("Keyboard", selected.empty())) {
            selected.clear();
            changed = true;
        }
        for (std::size_t index = 0; index < devices.size(); ++index) {
            const auto& device = devices[index];
            const std::string label = gamepadLabel(index);
            const bool active = selected == device.key;
            if (ImGui::Selectable(label.c_str(), active)) {
                selected = device.key;
                changed = true;
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (changed) {
        m_settings.gamepadGuids[m_selectedPlayer] = selected;
        try {
            m_session.input()->refresh();
        } catch (const std::exception& error) {
            setError(error.what());
        }
        saveSettings();
    }

    ImGui::SeparatorText("3DO controls");
    ImGui::TextWrapped("Left-click a binding to replace it; right-click to add another input; "
                       "middle-click to remove that input. Press a key/button or move a stick; Esc cancels.");
    if (ImGui::BeginTable("controller-bindings", 3,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("3DO button", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();

        for (const PadControl& control : padControls()) {
            const std::size_t button = control.retroId;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(m_selectedPlayer * PadButtonCount + button));
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(control.name);

            const auto drawBindingList = [&](const bool gamepad) {
                const auto& bindings = gamepad
                    ? m_settings.gamepadBindings[m_selectedPlayer][button]
                    : m_settings.keyboardBindings[m_selectedPlayer][button];
                const bool disabled = gamepad && m_session.input()->selectedGamepadId(m_selectedPlayer) == 0;
                const bool captureForThisField = m_captureBinding &&
                    m_captureBinding->player == m_selectedPlayer &&
                    m_captureBinding->button == button &&
                    m_captureBinding->gamepad == gamepad;
                const float cellWidth = ImGui::GetContentRegionAvail().x;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float horizontalPadding = ImGui::GetStyle().FramePadding.x * 2.0F;
                float lineWidth = 0.0F;
                bool hasButtonOnLine = false;
                bool removedBinding = false;

                ImGui::PushID(gamepad ? "gamepad" : "keyboard");
                ImGui::BeginDisabled(disabled);
                const std::size_t visibleBindingCount = std::max<std::size_t>(bindings.size(), 1);
                for (std::size_t index = 0; index < visibleBindingCount; ++index) {
                    const bool unbound = bindings.empty();
                    const bool capturingThisBinding = captureForThisField &&
                        !m_captureBinding->append &&
                        m_captureBinding->bindingIndex.value_or(0) == index;
                    std::string label;
                    if (capturingThisBinding) {
                        label = gamepad ? "Press input..." : "Press key...";
                    } else if (unbound) {
                        label = "Unbound";
                    } else if (gamepad) {
                        label = m_session.input()->gamepadBindingName(m_selectedPlayer, button, index);
                    } else {
                        const char* keyName =
                            SDL_GetScancodeName(static_cast<SDL_Scancode>(bindings[index]));
                        label = keyName && *keyName ? keyName : "Unbound";
                    }

                    const float buttonWidth =
                        ImGui::CalcTextSize(label.c_str()).x + horizontalPadding;
                    if (hasButtonOnLine && lineWidth + spacing + buttonWidth <= cellWidth) {
                        ImGui::SameLine();
                        lineWidth += spacing;
                    } else {
                        lineWidth = 0.0F;
                    }
                    ImGui::PushID(static_cast<int>(index));
                    const bool leftClicked = ImGui::Button(label.c_str());
                    const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                    const bool middleClicked = ImGui::IsItemClicked(ImGuiMouseButton_Middle);
                    ImGui::PopID();
                    hasButtonOnLine = true;
                    lineWidth += buttonWidth;

                    if (middleClicked && !unbound) {
                        if (gamepad) m_session.input()->removeGamepadBinding(m_selectedPlayer, button, index);
                        else m_session.input()->removeKeyboardBinding(m_selectedPlayer, button, index);
                        m_captureBinding.reset();
                        saveSettings();
                        m_status = "Input binding removed.";
                        removedBinding = true;
                        break;
                    }
                    if (rightClicked) {
                        m_captureBinding = BindingCapture{m_selectedPlayer, button, gamepad,
                                                         std::nullopt, true};
                    } else if (leftClicked) {
                        m_captureBinding = BindingCapture{m_selectedPlayer, button, gamepad,
                            unbound ? std::nullopt : std::optional<std::size_t>(index), false};
                    }
                }

                if (captureForThisField && m_captureBinding->append) {
                    const std::string label = gamepad ? "Press new input..." : "Press new key...";
                    const float buttonWidth =
                        ImGui::CalcTextSize(label.c_str()).x + horizontalPadding;
                    if (hasButtonOnLine && lineWidth + spacing + buttonWidth <= cellWidth) {
                        ImGui::SameLine();
                    }
                    ImGui::BeginDisabled();
                    ImGui::Button(label.c_str());
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                if (removedBinding) return;
            };

            ImGui::TableSetColumnIndex(1);
            drawBindingList(false);

            ImGui::TableSetColumnIndex(2);
            drawBindingList(true);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (m_session.input()->selectedGamepadId(m_selectedPlayer) == 0) {
        ImGui::TextWrapped("Select a connected gamepad above to remap its controls.");
    }
}

} // namespace app
