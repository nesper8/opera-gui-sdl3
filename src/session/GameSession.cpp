#include "app/session/GameSession.hpp"

#include "app/audio/AudioOutput.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/input/InputManager.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>

namespace app {

GameSession::~GameSession() = default;

void GameSession::initialize(Settings& settings) {
    if (m_input || m_audio) {
        throw std::logic_error("The game session is already initialized.");
    }
    auto input = std::make_unique<InputManager>(settings);
    input->refresh();
    auto audio = std::make_unique<AudioOutput>(settings);
    m_input = std::move(input);
    m_audio = std::move(audio);
}

void GameSession::loadCore(const std::filesystem::path& corePath,
                           const std::filesystem::path& systemDirectory,
                           const std::filesystem::path& saveDirectory,
                           Settings& settings,
                           CoreCallbacks callbacks) {
    auto core = std::make_unique<LibretroCore>(systemDirectory, saveDirectory, std::move(callbacks));
    core->load(corePath);
    for (const CoreOption& option : core->options()) {
        const auto saved = settings.coreOptions.find(option.key);
        if (saved != settings.coreOptions.end() &&
            std::find(option.values.begin(), option.values.end(), saved->second) != option.values.end()) {
            core->setOption(option.key, saved->second);
        } else if (!option.values.empty()) {
            settings.coreOptions[option.key] = option.currentValue;
        }
    }
    m_core = std::move(core);
}

void GameSession::loadGame(const std::filesystem::path& path) {
    if (!m_core || !m_audio) {
        throw std::logic_error("Load the Opera core before loading a game.");
    }
    m_core->loadGame(path);
    m_audio->configure(m_core->sampleRate());
}

void GameSession::unloadCore() noexcept {
    m_core.reset();
}

InputManager* GameSession::input() noexcept {
    return m_input.get();
}

AudioOutput* GameSession::audio() noexcept {
    return m_audio.get();
}

LibretroCore* GameSession::core() noexcept {
    return m_core.get();
}

} // namespace app
