#include "app/InputManager.hpp"

#include "app/Logger.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_map>

namespace app {
namespace {

} // namespace

InputManager::InputManager(Settings& settings) : m_settings(settings) {}

InputManager::~InputManager() {
    for (SDL_Gamepad* gamepad : m_openGamepads) SDL_CloseGamepad(gamepad);
}

void InputManager::refresh() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (count < 0 || (count > 0 && ids == nullptr)) {
        throw std::runtime_error(std::string("Unable to enumerate gamepads: ") + SDL_GetError());
    }

    std::vector<SDL_Gamepad*> open;
    std::vector<GamepadInfo> devices;
    std::array<SDL_Gamepad*, 2> selected{};
    std::unordered_map<std::string, unsigned> guidOccurrences;
    try {
        for (int index = 0; index < count; ++index) {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[index]);
            if (gamepad == nullptr) {
                Logger::write(LogLevel::Warning, std::string("Unable to open gamepad: ") + SDL_GetError());
                continue;
            }
            open.push_back(gamepad);
            char guid[64]{};
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[index]), guid, static_cast<int>(sizeof(guid)));
            const std::string guidString(guid);
            const unsigned occurrence = guidOccurrences[guidString]++;
            GamepadInfo info{ids[index], guidString + "#" + std::to_string(occurrence), guidString,
                             SDL_GetGamepadName(gamepad) ? SDL_GetGamepadName(gamepad) : "Gamepad"};
            devices.push_back(info);
            for (std::size_t player = 0; player < selected.size(); ++player) {
                if (!selected[player] && !m_settings.gamepadGuids[player].empty() &&
                    info.key == m_settings.gamepadGuids[player]) {
                    selected[player] = gamepad;
                }
            }
        }
    } catch (...) {
        for (SDL_Gamepad* gamepad : open) SDL_CloseGamepad(gamepad);
        SDL_free(ids);
        throw;
    }
    SDL_free(ids);
    for (SDL_Gamepad* gamepad : m_openGamepads) SDL_CloseGamepad(gamepad);
    m_openGamepads = std::move(open);
    m_gamepads = std::move(devices);
    m_selectedGamepads = selected;
}

std::vector<GamepadInfo> InputManager::gamepads() const {
    return m_gamepads;
}

void InputManager::setKeyboardBinding(const std::size_t player, const std::size_t button, const SDL_Scancode key) {
    if (player >= m_settings.keyboardBindings.size() || button >= PadButtonCount) {
        throw std::out_of_range("Keyboard binding is outside the supported controller range.");
    }
    m_settings.keyboardBindings[player][button] = static_cast<int>(key);
}

void InputManager::setGamepadButtonBinding(const std::size_t player, const std::size_t button,
                                           const SDL_GamepadButton gamepadButton) {
    if (player >= m_settings.gamepadBindings.size() || button >= PadButtonCount ||
        gamepadButton < 0 || gamepadButton >= SDL_GAMEPAD_BUTTON_COUNT) {
        throw std::out_of_range("Gamepad button binding is outside the supported range.");
    }
    m_settings.gamepadBindings[player][button] = static_cast<int>(gamepadButton);
}

void InputManager::setGamepadAxisBinding(const std::size_t player, const std::size_t button,
                                         const SDL_GamepadAxis axis) {
    if (player >= m_settings.gamepadBindings.size() || button >= PadButtonCount ||
        axis < SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis > SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        throw std::out_of_range("Only gamepad trigger axes can be bound to digital buttons.");
    }
    m_settings.gamepadBindings[player][button] = -static_cast<int>(axis) - 2;
}

SDL_JoystickID InputManager::selectedGamepadId(const std::size_t player) const noexcept {
    if (player >= m_selectedGamepads.size() || !m_selectedGamepads[player]) return 0;
    return SDL_GetGamepadID(m_selectedGamepads[player]);
}

std::string InputManager::gamepadBindingName(const std::size_t player, const std::size_t button) const {
    if (player >= m_settings.gamepadBindings.size() || button >= PadButtonCount) return "Unbound";
    const int binding = m_settings.gamepadBindings[player][button];
    if (binding >= 0 && binding < SDL_GAMEPAD_BUTTON_COUNT) {
        const auto label = m_selectedGamepads[player]
            ? SDL_GetGamepadButtonLabel(m_selectedGamepads[player],
                                        static_cast<SDL_GamepadButton>(binding))
            : SDL_GetGamepadButtonLabelForType(SDL_GAMEPAD_TYPE_XBOX360,
                                               static_cast<SDL_GamepadButton>(binding));
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
        default: break;
        }
        switch (static_cast<SDL_GamepadButton>(binding)) {
        case SDL_GAMEPAD_BUTTON_BACK: return "Back";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
        default: {
            const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding));
            return name ? name : "Unknown";
        }
        }
    }
    if (binding <= -2) {
        const int axis = -binding - 2;
        if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return "Left Trigger";
        if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return "Right Trigger";
    }
    return "Unbound";
}

std::int16_t InputManager::buttonState(const unsigned player, const unsigned button) const noexcept {
    if (player >= m_settings.keyboardBindings.size() || button >= PadButtonCount) return 0;
    const std::size_t playerIndex = static_cast<std::size_t>(player);

    if (SDL_Gamepad* gamepad = m_selectedGamepads[playerIndex]) {
        const int binding = m_settings.gamepadBindings[playerIndex][button];
        if (binding >= 0 && binding < SDL_GAMEPAD_BUTTON_COUNT) {
            return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(binding)) ? 1 : 0;
        }
        if (binding <= -2) {
            const int axis = -binding - 2;
            if (axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER && axis <= SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                return SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(axis)) > 16000 ? 1 : 0;
            }
        }
        return 0;
    }

    int keyCount = 0;
    const bool* keyboard = SDL_GetKeyboardState(&keyCount);
    const int key = m_settings.keyboardBindings[playerIndex][button];
    return key >= 0 && key < keyCount && keyboard[key] ? 1 : 0;
}

} // namespace app
