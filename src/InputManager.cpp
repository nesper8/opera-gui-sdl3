#include "app/InputManager.hpp"

#include "app/Logger.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_map>

namespace app {
namespace {

constexpr std::array<SDL_GamepadButton, PadButtonCount> gamepadButtons{
    SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK
};

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

std::int16_t InputManager::buttonState(const unsigned player, const unsigned button) const noexcept {
    if (player >= m_settings.keyboardBindings.size() || button >= PadButtonCount) return 0;
    const std::size_t playerIndex = static_cast<std::size_t>(player);

    if (SDL_Gamepad* gamepad = m_selectedGamepads[playerIndex]) {
        if (gamepadButtons[button] != SDL_GAMEPAD_BUTTON_INVALID &&
            SDL_GetGamepadButton(gamepad, gamepadButtons[button])) return 1;
        if (button == 12 && SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) return 1;
        if (button == 13 && SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) return 1;
        return 0;
    }

    int keyCount = 0;
    const bool* keyboard = SDL_GetKeyboardState(&keyCount);
    const int key = m_settings.keyboardBindings[playerIndex][button];
    return key >= 0 && key < keyCount && keyboard[key] ? 1 : 0;
}

} // namespace app
