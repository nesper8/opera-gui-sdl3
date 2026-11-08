#include "app/application/Application.hpp"

#include "app/audio/AudioOutput.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/input/InputManager.hpp"
#include "app/logging/Logger.hpp"
#include "app/platform/FrontendRuntime.hpp"

#include <windows.h>

#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace app {
namespace {

std::filesystem::path pathFromUtf8(const char* value) {
    if (!value) throw std::invalid_argument("Cannot create a path from a null UTF-8 string.");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length <= 0) throw std::runtime_error("SDL returned a path that is not valid UTF-8.");
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                            wide.data(), length) != length) {
        throw std::runtime_error("Unable to convert an SDL path from UTF-8.");
    }
    wide.pop_back();
    return std::filesystem::path(std::move(wide));
}

std::optional<std::filesystem::path> detectOperaBios(const std::filesystem::path& directory) {
    static constexpr std::array<const char*, 10> biosFiles{
        "panafz1.bin",
        "panafz10.bin",
        "panafz10-norsa.bin",
        "panafz10e-anvil.bin",
        "panafz10e-anvil-norsa.bin",
        "panafz1j.bin",
        "panafz1j-norsa.bin",
        "goldstar.bin",
        "sanyotry.bin",
        "3do_arcade_saot.bin"
    };
    std::array<bool, biosFiles.size()> detected{};
    std::error_code error;
    std::filesystem::directory_iterator entry(directory, error);
    if (error) {
        throw std::filesystem::filesystem_error("Unable to inspect Opera BIOS directory",
                                                directory, error);
    }
    const std::filesystem::directory_iterator end;
    while (entry != end) {
        const auto filename = entry->path().filename();
        for (std::size_t index = 0; index < biosFiles.size(); ++index) {
            if (filename != biosFiles[index]) continue;
            const bool isFile = entry->is_regular_file(error);
            if (error) {
                throw std::filesystem::filesystem_error("Unable to inspect Opera BIOS file",
                                                        entry->path(), error);
            }
            detected[index] = isFile;
        }
        entry.increment(error);
        if (error) {
            throw std::filesystem::filesystem_error("Unable to inspect Opera BIOS directory",
                                                    directory, error);
        }
    }
    for (std::size_t index = 0; index < biosFiles.size(); ++index) {
        if (detected[index]) return directory / biosFiles[index];
    }
    return std::nullopt;
}

} // namespace

std::string Application::pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t byte : value) result.push_back(static_cast<char>(byte));
    return result;
}

Application::Application() {
    const char* basePath = SDL_GetBasePath();
    if (!basePath) throw std::runtime_error(std::string("Unable to locate the application directory: ") + SDL_GetError());
    m_baseDirectory = pathFromUtf8(basePath);
    SDL_free(const_cast<char*>(basePath));

    const char* preferencesPath = SDL_GetPrefPath("Opera", "OperaSDL3Frontend");
    if (!preferencesPath) throw std::runtime_error(std::string("Unable to locate the settings directory: ") + SDL_GetError());
    m_preferencesDirectory = pathFromUtf8(preferencesPath);
    SDL_free(const_cast<char*>(preferencesPath));
    m_settingsFile = m_preferencesDirectory / "settings.ini";
    m_coreDirectory = m_baseDirectory / "cores";
    m_biosDirectory = m_baseDirectory / "bios";
    m_systemDirectory = m_preferencesDirectory / "system";
    m_saveDirectory = m_preferencesDirectory / "saves";
    m_imguiIniFile = pathToUtf8(m_preferencesDirectory / "imgui.ini");
    std::filesystem::create_directories(m_systemDirectory);
    std::filesystem::create_directories(m_biosDirectory);
    std::filesystem::create_directories(m_saveDirectory);

    Logger::initialize(m_preferencesDirectory / "opera-frontend.log");
    m_settings = Settings::load(m_settingsFile);
    m_detectedBios = detectOperaBios(m_biosDirectory);
    if (m_settings.useDetectedBios && m_detectedBios) {
        m_systemDirectory = m_biosDirectory;
    }
}

Application::~Application() {
    try {
        saveSettings();
    } catch (const std::exception& error) {
        Logger::write(LogLevel::Error, std::string("Unable to save settings during shutdown: ") + error.what());
    }
    m_session.unloadCore();
    if (m_frameTexture) SDL_DestroyTexture(m_frameTexture);
}

int Application::run() {
    initialize();
    using Clock = std::chrono::steady_clock;
    auto nextCoreFrame = Clock::now();
    while (m_running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) handleEvent(event);
        checkDownload();
        if (m_pendingGame) {
            const auto selectedGame = std::move(*m_pendingGame);
            m_pendingGame.reset();
            try {
                loadGame(selectedGame);
            } catch (const std::exception& error) {
                setError(error.what());
            }
        }
        if (m_session.core() && m_session.core()->gameLoaded()) {
            const auto frameDuration = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / m_session.core()->frameRate()));
            constexpr unsigned MaxCatchUpFrames = 4;
            unsigned catchUpFrames = 0;
            auto now = Clock::now();
            while (now >= nextCoreFrame && catchUpFrames < MaxCatchUpFrames) {
                m_session.core()->run();
                nextCoreFrame += frameDuration;
                ++catchUpFrames;
                now = Clock::now();
            }
        } else {
            nextCoreFrame = Clock::now();
        }
        render();
        if (m_session.core() && m_session.core()->gameLoaded() && !m_runtime.vsyncActive()) {
            std::this_thread::sleep_until(nextCoreFrame);
        } else if ((!m_session.core() || !m_session.core()->gameLoaded()) && !m_runtime.vsyncActive()) {
            SDL_Delay(16);
        }
    }
    return 0;
}

void Application::initialize() {
    m_runtime.initialize(m_settings, m_imguiIniFile);
    if (!m_runtime.vsyncActive()) {
        m_settings.vsync = false;
        Logger::write(LogLevel::Warning, "VSync could not be configured.");
    }

    m_session.initialize(m_settings);
    try {
        m_audioDevices = AudioOutput::playbackDevices();
    } catch (const std::exception& error) {
        setError(error.what());
    }
    if (std::filesystem::exists(m_coreDirectory / "opera_libretro.dll")) {
        try {
            loadCore();
        } catch (const std::exception& error) {
            setError(error.what());
        }
    }
}

void Application::handleEvent(const SDL_Event& event) {
    m_runtime.processEvent(event);
    const bool bindingWasActive = m_captureBinding.has_value();
    if (m_captureBinding && event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE) {
        m_captureBinding.reset();
        m_status = "Binding cancelled.";
    } else if (m_captureBinding && !m_captureBinding->gamepad &&
               event.type == SDL_EVENT_KEY_DOWN) {
        m_session.input()->setKeyboardBinding(m_captureBinding->player, m_captureBinding->button,
                                    event.key.scancode, m_captureBinding->bindingIndex);
        m_captureBinding.reset();
        saveSettings();
        m_status = "Keyboard binding updated.";
    } else if (m_captureBinding && m_captureBinding->gamepad &&
               event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               event.gbutton.which == m_session.input()->selectedGamepadId(m_captureBinding->player)) {
        m_session.input()->setGamepadButtonBinding(m_captureBinding->player, m_captureBinding->button,
                   static_cast<SDL_GamepadButton>(event.gbutton.button), m_captureBinding->bindingIndex);
        m_captureBinding.reset();
        saveSettings();
        m_status = "Gamepad binding updated.";
    } else if (m_captureBinding && m_captureBinding->gamepad &&
               event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               event.gaxis.which == m_session.input()->selectedGamepadId(m_captureBinding->player) &&
               (event.gaxis.value > 16000 || event.gaxis.value < -16000)) {
        m_session.input()->setGamepadAxisBinding(m_captureBinding->player, m_captureBinding->button,
                                       static_cast<SDL_GamepadAxis>(event.gaxis.axis),
                                       event.gaxis.value < 0, m_captureBinding->bindingIndex);
        m_captureBinding.reset();
        saveSettings();
        m_status = "Gamepad axis binding updated.";
    }
    if (event.type == SDL_EVENT_QUIT) {
        m_running = false;
    } else if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        try {
            m_session.input()->refresh();
        } catch (const std::exception& error) {
            setError(error.what());
        }
    } else if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED ||
               event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
        try {
            m_audioDevices = AudioOutput::playbackDevices();
        } catch (const std::exception& error) {
            setError(error.what());
        }
    } else if (!bindingWasActive && event.type == SDL_EVENT_KEY_DOWN &&
               event.key.key == SDLK_F2 && !event.key.repeat) {
        m_uiVisible = !m_uiVisible;
        m_captureBinding.reset();
    } else if (!bindingWasActive && event.type == SDL_EVENT_KEY_DOWN &&
               event.key.key == SDLK_F3 && !event.key.repeat) {
        m_settings.showFpsCounter = !m_settings.showFpsCounter;
        saveSettings();
        m_status = m_settings.showFpsCounter ? "FPS counter enabled." : "FPS counter disabled.";
    } else if (!bindingWasActive && event.type == SDL_EVENT_KEY_DOWN &&
               event.key.key == SDLK_F11 && !event.key.repeat) {
        m_settings.fullscreen = !m_settings.fullscreen;
        if (!m_runtime.setFullscreen(m_settings.fullscreen)) setError(SDL_GetError());
        saveSettings();
    } else if (!bindingWasActive && event.type == SDL_EVENT_KEY_DOWN &&
               event.key.key == SDLK_F1 && !event.key.repeat) {
        if (m_session.core()) m_session.core()->reset();
    }
}

void Application::openGameDialog() {
    static constexpr SDL_DialogFileFilter filters[]{
        {"3DO games", "iso;cue;bin"},
        {"ISO disc images", "iso"},
        {"BIN/CUE disc images", "cue;bin"}
    };
    SDL_ShowOpenFileDialog(&Application::fileDialogCallback, this, m_runtime.window(),
                           filters, static_cast<int>(std::size(filters)), nullptr, false);
}

void Application::fileDialogCallback(void* userdata, const char* const* filelist, int) {
    auto* self = static_cast<Application*>(userdata);
    if (!self || !filelist || !filelist[0]) return;
    try {
        self->m_pendingGame = pathFromUtf8(filelist[0]);
    } catch (const std::exception& error) {
        self->setError(error.what());
    } catch (...) {
        self->setError("The selected game path could not be read.");
    }
}

} // namespace app
