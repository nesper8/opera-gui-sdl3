#pragma once

#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

namespace app::ui {

inline bool selectableCombo(
    const char* label, std::string& value,
    const std::vector<std::pair<const char*, const char*>>& choices) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value.c_str())) {
        for (const auto& [id, name] : choices) {
            const bool selected = value == id;
            if (ImGui::Selectable(name, selected)) {
                value = id;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace app::ui
