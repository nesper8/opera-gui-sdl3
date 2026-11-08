#pragma once

#include "app/settings/Settings.hpp"

#include <filesystem>
#include <memory>

namespace app {

class AudioOutput;
class InputManager;
class LibretroCore;
struct CoreCallbacks;

class GameSession final {
public:
    GameSession() = default;
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    void initialize(Settings& settings);
    void loadCore(const std::filesystem::path& corePath,
                  const std::filesystem::path& systemDirectory,
                  const std::filesystem::path& saveDirectory,
                  Settings& settings,
                  CoreCallbacks callbacks);
    void loadGame(const std::filesystem::path& path);
    void unloadCore() noexcept;

    InputManager* input() noexcept;
    AudioOutput* audio() noexcept;
    LibretroCore* core() noexcept;

private:
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<AudioOutput> m_audio;
    std::unique_ptr<LibretroCore> m_core;
};

} // namespace app
