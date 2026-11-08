#pragma once

#include "app/Settings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

namespace app {

struct GamepadInfo final {
    SDL_JoystickID id{};
    std::string key;
    std::string guid;
    std::string name;
};

class InputManager final {
public:
    explicit InputManager(Settings& settings);
    ~InputManager();

    void refresh();
    std::vector<GamepadInfo> gamepads() const;
    void setKeyboardBinding(std::size_t player, std::size_t button, SDL_Scancode key);
    void setGamepadButtonBinding(std::size_t player, std::size_t button, SDL_GamepadButton gamepadButton);
    void setGamepadAxisBinding(std::size_t player, std::size_t button, SDL_GamepadAxis axis);
    SDL_JoystickID selectedGamepadId(std::size_t player) const noexcept;
    std::string gamepadBindingName(std::size_t player, std::size_t button) const;
    std::int16_t buttonState(unsigned player, unsigned button) const noexcept;

private:
    Settings& m_settings;
    std::vector<GamepadInfo> m_gamepads;
    std::array<SDL_Gamepad*, 2> m_selectedGamepads{};
    std::vector<SDL_Gamepad*> m_openGamepads;
};

} // namespace app
