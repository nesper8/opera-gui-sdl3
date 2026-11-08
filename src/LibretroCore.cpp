#include "app/LibretroCore.hpp"

#include "app/Logger.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace app {
using namespace libretro;

namespace {

std::string utf8Path(const std::filesystem::path& path) {
    const std::wstring wide = path.wstring();
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(),
                                          static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Unable to convert a file path to UTF-8.");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(),
                            static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr) != size) {
        throw std::runtime_error("Unable to convert a file path to UTF-8.");
    }
    return result;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return std::string(text.substr(first, last - first + 1));
}

std::vector<std::string> splitValues(std::string_view text) {
    std::vector<std::string> values;
    while (!text.empty()) {
        const auto separator = text.find('|');
        std::string value = trim(text.substr(0, separator));
        if (!value.empty()) values.push_back(std::move(value));
        if (separator == std::string_view::npos) break;
        text.remove_prefix(separator + 1);
    }
    return values;
}

template <typename Callback, typename ErrorCallback>
void protectCallback(const char* name, Callback&& callback, ErrorCallback&& onError) noexcept {
    try {
        callback();
    } catch (const std::exception& error) {
        try {
            const std::string message = std::string("libretro ") + name +
                " callback failed: " + error.what();
            Logger::write(LogLevel::Error, message);
            onError(message);
        } catch (...) {
            Logger::write(LogLevel::Error, "Unable to report a libretro callback error.");
        }
    } catch (...) {
        try {
            const std::string message = std::string("libretro ") + name +
                " callback failed with an unknown error.";
            Logger::write(LogLevel::Error, message);
            onError(message);
        } catch (...) {
            Logger::write(LogLevel::Error, "Unable to report a libretro callback error.");
        }
    }
}

} // namespace

LibretroCore* LibretroCore::s_active = nullptr;

LibretroCore::LibretroCore(std::filesystem::path systemDirectory,
                           std::filesystem::path saveDirectory,
                           CoreCallbacks callbacks)
    : m_systemDirectory(std::move(systemDirectory)),
      m_saveDirectory(std::move(saveDirectory)),
      m_callbacks(std::move(callbacks)) {}

LibretroCore::~LibretroCore() {
    if (m_gameLoaded && m_unloadGame) m_unloadGame();
    if (m_initialized && m_deinit) m_deinit();
    if (s_active == this) s_active = nullptr;
    if (m_library) FreeLibrary(static_cast<HMODULE>(m_library));
}

template <typename Function>
Function LibretroCore::requiredSymbol(const char* name) {
    const auto symbol = reinterpret_cast<Function>(GetProcAddress(static_cast<HMODULE>(m_library), name));
    if (!symbol) throw std::runtime_error(std::string("The Opera core is missing required symbol ") + name + ".");
    return symbol;
}

void LibretroCore::load(const std::filesystem::path& coreFile) {
    if (m_library) throw std::logic_error("An Opera core is already loaded.");
    if (!std::filesystem::is_regular_file(coreFile)) {
        throw std::runtime_error("Opera core DLL was not found: " + coreFile.string());
    }

    m_library = LoadLibraryW(coreFile.c_str());
    if (!m_library) {
        throw std::runtime_error("Unable to load Opera core DLL (Windows error " +
                                 std::to_string(GetLastError()) + "): " + coreFile.string());
    }

    try {
        s_active = this;
        m_setEnvironment = requiredSymbol<EnvironmentFn>("retro_set_environment");
        m_setVideoRefresh = requiredSymbol<VideoFn>("retro_set_video_refresh");
        m_setAudioSample = requiredSymbol<AudioFn>("retro_set_audio_sample");
        m_setAudioSampleBatch = requiredSymbol<AudioBatchFn>("retro_set_audio_sample_batch");
        m_setInputPoll = requiredSymbol<InputPollFn>("retro_set_input_poll");
        m_setInputState = requiredSymbol<InputStateFn>("retro_set_input_state");
        m_init = requiredSymbol<decltype(m_init)>("retro_init");
        m_deinit = requiredSymbol<decltype(m_deinit)>("retro_deinit");
        m_apiVersion = requiredSymbol<decltype(m_apiVersion)>("retro_api_version");
        m_getSystemInfo = requiredSymbol<decltype(m_getSystemInfo)>("retro_get_system_info");
        m_getSystemAvInfo = requiredSymbol<decltype(m_getSystemAvInfo)>("retro_get_system_av_info");
        m_loadGame = requiredSymbol<decltype(m_loadGame)>("retro_load_game");
        m_unloadGame = requiredSymbol<decltype(m_unloadGame)>("retro_unload_game");
        m_run = requiredSymbol<decltype(m_run)>("retro_run");
        m_reset = requiredSymbol<decltype(m_reset)>("retro_reset");
        m_setControllerPortDevice = reinterpret_cast<decltype(m_setControllerPortDevice)>(
            GetProcAddress(static_cast<HMODULE>(m_library), "retro_set_controller_port_device"));

        if (m_apiVersion() != 1) throw std::runtime_error("The Opera core uses an unsupported libretro API version.");
        m_setEnvironment(&LibretroCore::environment);
        m_setVideoRefresh(&LibretroCore::videoRefresh);
        m_setAudioSample(&LibretroCore::audioSample);
        m_setAudioSampleBatch(&LibretroCore::audioSampleBatch);
        m_setInputPoll(&LibretroCore::inputPoll);
        m_setInputState(&LibretroCore::inputState);
        Logger::write(LogLevel::Info, "Initializing Opera core.");
        m_init();
        m_initialized = true;

        Logger::write(LogLevel::Info, "Reading Opera core metadata.");
        SystemInfo info{};
        m_getSystemInfo(&info);
        m_coreName = info.library_name ? info.library_name : "Opera";
        m_coreVersion = info.library_version ? info.library_version : "unknown";
        Logger::write(LogLevel::Info, "Loaded " + m_coreName + " core " + m_coreVersion + ".");
    } catch (...) {
        if (m_initialized && m_deinit) {
            m_deinit();
            m_initialized = false;
        }
        s_active = nullptr;
        FreeLibrary(static_cast<HMODULE>(m_library));
        m_library = nullptr;
        throw;
    }
}

void LibretroCore::loadGame(const std::filesystem::path& gameFile) {
    if (!m_library || !m_initialized) throw std::logic_error("Load the Opera core before loading a game.");
    if (m_gameLoaded) {
        m_unloadGame();
        m_gameLoaded = false;
    }

    const std::string path = utf8Path(gameFile);
    const GameInfo game{path.c_str(), nullptr, 0, nullptr};
    if (!m_loadGame(&game)) throw std::runtime_error("Opera core could not load game: " + gameFile.string());
    m_gameLoaded = true;

    SystemAvInfo avInfo{};
    m_getSystemAvInfo(&avInfo);
    m_frameRate = avInfo.timing.fps;
    m_sampleRate = avInfo.timing.sample_rate;
    if (!std::isfinite(m_frameRate) || m_frameRate < 1.0 || m_frameRate > 1000.0 ||
        !std::isfinite(m_sampleRate) || m_sampleRate < 8000.0 || m_sampleRate > 192000.0) {
        m_unloadGame();
        m_gameLoaded = false;
        throw std::runtime_error("Opera core reported invalid audio/video timing.");
    }
    if (m_setControllerPortDevice) {
        m_setControllerPortDevice(0, DeviceJoypad);
        m_setControllerPortDevice(1, DeviceJoypad);
    }
}

void LibretroCore::run() {
    if (!m_gameLoaded) return;
    m_run();
}

void LibretroCore::reset() {
    if (m_gameLoaded) m_reset();
}

void LibretroCore::setOption(const std::string& key, const std::string& value) {
    const auto option = std::find_if(m_options.begin(), m_options.end(),
                                     [&key](const CoreOption& item) { return item.key == key; });
    if (option == m_options.end() ||
        std::find(option->values.begin(), option->values.end(), value) == option->values.end()) {
        throw std::invalid_argument("The requested value is not available for this core option.");
    }
    m_variables.insert_or_assign(key, value);
    option->currentValue = value;
    m_variableUpdated = true;
}

void LibretroCore::parseOptions(const Variable* variables) {
    m_options.clear();
    m_variables.clear();
    if (!variables) return;
    for (const Variable* variable = variables; variable->key && variable->value; ++variable) {
        const std::string description = variable->value;
        const auto separator = description.find(';');
        CoreOption option;
        option.key = variable->key;
        option.description = separator == std::string::npos
            ? option.key : trim(std::string_view(description).substr(0, separator));
        option.values = splitValues(separator == std::string::npos
            ? std::string_view{} : std::string_view(description).substr(separator + 1));
        if (option.values.empty()) continue;
        option.currentValue = option.values.front();
        m_variables.insert_or_assign(option.key, option.currentValue);
        m_options.push_back(std::move(option));
    }
    if (m_callbacks.optionsChanged) m_callbacks.optionsChanged();
}

void LibretroCore::parseCoreOptions(const CoreOptionDefinition* definitions) {
    m_options.clear();
    m_variables.clear();
    if (!definitions) return;
    for (const auto* definition = definitions; definition->key; ++definition) {
        CoreOption option;
        option.key = definition->key;
        option.description = definition->description ? definition->description : option.key;
        for (const auto& value : definition->values) {
            if (!value.value) break;
            option.values.emplace_back(value.value);
        }
        if (option.values.empty()) continue;
        option.currentValue = definition->defaultValue ? definition->defaultValue : option.values.front();
        if (std::find(option.values.begin(), option.values.end(), option.currentValue) == option.values.end()) {
            option.currentValue = option.values.front();
        }
        m_variables.insert_or_assign(option.key, option.currentValue);
        m_options.push_back(std::move(option));
    }
    if (m_callbacks.optionsChanged) m_callbacks.optionsChanged();
}

bool LibretroCore::environment(const unsigned command, void* data) noexcept {
    auto* self = s_active;
    if (!self) return false;
    bool result = false;
    protectCallback("environment", [&] {
        switch (command) {
        case GetCanDupe:
            if (data) *static_cast<bool*>(data) = true;
            result = data != nullptr;
            break;
        case GetOverscan:
            if (data) *static_cast<bool*>(data) = false;
            result = data != nullptr;
            break;
        case SetPixelFormat:
            if (data) {
                const auto format = *static_cast<unsigned*>(data);
                result = format == Pixel0Rgb1555 || format == PixelXrgb8888 || format == PixelRgb565;
                if (result && self->m_callbacks.pixelFormat) self->m_callbacks.pixelFormat(format);
            }
            break;
        case SetVariables:
            self->parseOptions(static_cast<const Variable*>(data));
            result = true;
            break;
        case SetCoreOptionsVersion:
            if (data) *static_cast<unsigned*>(data) = 1;
            result = data != nullptr;
            break;
        case SetCoreOptions:
            self->parseCoreOptions(static_cast<const CoreOptionDefinition*>(data));
            result = true;
            break;
        case SetCoreOptionsIntl:
            if (data) {
                const auto* options = static_cast<const CoreOptionsIntl*>(data);
                self->parseCoreOptions(options->us ? options->us : options->local);
                result = true;
            }
            break;
        case GetVariable: {
            auto* variable = static_cast<Variable*>(data);
            result = true;
            if (variable) {
                variable->value = nullptr;
                if (variable->key) {
                    const auto found = self->m_variables.find(variable->key);
                    if (found != self->m_variables.end()) {
                        variable->value = found->second.c_str();
                    } else {
                        Logger::write(LogLevel::Warning,
                            std::string("Opera core requested an undefined option: ") + variable->key);
                    }
                }
            }
            break;
        }
        case GetVariableUpdate:
            if (data) {
                *static_cast<bool*>(data) = self->m_variableUpdated;
                self->m_variableUpdated = false;
                result = true;
            }
            break;
        case GetInputBitmasks:
            result = true;
            break;
        case GetInputMaxUsers:
            if (data) *static_cast<unsigned*>(data) = 2;
            result = data != nullptr;
            break;
        case GetLogInterface:
            if (data) {
                static_cast<LogCallback*>(data)->log = &LibretroCore::coreLog;
                result = true;
            }
            break;
        case GetSystemDirectoryPath:
        case GetCoreAssetsDirectory:
            if (data) {
                static thread_local std::string path;
                path = utf8Path(self->m_systemDirectory);
                *static_cast<const char**>(data) = path.c_str();
                result = true;
            }
            break;
        case GetSaveDirectory:
            if (data) {
                static thread_local std::string path;
                path = utf8Path(self->m_saveDirectory);
                *static_cast<const char**>(data) = path.c_str();
                result = true;
            }
            break;
        case SetSupportNoGame:
        case SetSupportAchievements:
        case SetInputDescriptors:
            result = true;
            break;
        case SetMessage:
        case SetPerformanceLevel:
        case GetLanguage:
        case SetRotation:
            break;
        case SetHardwareRender:
            Logger::write(LogLevel::Warning, "The Opera frontend supports software-rendered cores only.");
            break;
        default:
            break;
        }
    }, [self](const std::string& error) {
        if (self->m_callbacks.error) self->m_callbacks.error(error);
    });
    return result;
}

void LibretroCore::coreLog(const int level, const char* format, ...) noexcept {
    if (!format) return;
    std::array<char, 2048> buffer{};
    std::va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    if (written < 0) {
        Logger::write(LogLevel::Error, "Opera core emitted an invalid log message.");
        return;
    }

    LogLevel logLevel = LogLevel::Info;
    if (level >= 3) logLevel = LogLevel::Error;
    else if (level == 2) logLevel = LogLevel::Warning;
    Logger::write(logLevel, std::string("Opera core: ") + buffer.data());
}

void LibretroCore::videoRefresh(const void* data, const unsigned width, const unsigned height,
                                const std::size_t pitch) noexcept {
    auto* self = s_active;
    if (!self || !self->m_callbacks.video) return;
    protectCallback("video", [&] { self->m_callbacks.video(data, width, height, pitch); },
                    [self](const std::string& error) {
                        if (self->m_callbacks.error) self->m_callbacks.error(error);
                    });
}

void LibretroCore::audioSample(const std::int16_t left, const std::int16_t right) noexcept {
    auto* self = s_active;
    if (!self || !self->m_callbacks.audio) return;
    const std::array<std::int16_t, 2> sample{left, right};
    protectCallback("audio", [&] { self->m_callbacks.audio(sample.data(), 1); },
                    [self](const std::string& error) {
                        if (self->m_callbacks.error) self->m_callbacks.error(error);
                    });
}

std::size_t LibretroCore::audioSampleBatch(const std::int16_t* data, const std::size_t frames) noexcept {
    auto* self = s_active;
    if (!self || !self->m_callbacks.audio) return 0;
    std::size_t accepted = 0;
    protectCallback("audio batch", [&] {
        self->m_callbacks.audio(data, frames);
        accepted = frames;
    }, [self](const std::string& error) {
        if (self->m_callbacks.error) self->m_callbacks.error(error);
    });
    return accepted;
}

void LibretroCore::inputPoll() noexcept {
    auto* self = s_active;
    if (!self || !self->m_callbacks.inputPoll) return;
    protectCallback("input poll", [&] { self->m_callbacks.inputPoll(); },
                    [self](const std::string& error) {
                        if (self->m_callbacks.error) self->m_callbacks.error(error);
                    });
}

std::int16_t LibretroCore::inputState(const unsigned port, const unsigned device,
                                      const unsigned, const unsigned id) noexcept {
    auto* self = s_active;
    if (!self || device != DeviceJoypad || port > 1 || !self->m_callbacks.inputState) return 0;
    std::int16_t state = 0;
    protectCallback("input state", [&] {
        if (id == IdJoypadMask) {
            std::uint16_t mask = 0;
            for (unsigned button = 0; button <= IdJoypadR3; ++button) {
                if (self->m_callbacks.inputState(port, button)) {
                    mask |= static_cast<std::uint16_t>(1U << button);
                }
            }
            state = static_cast<std::int16_t>(mask);
        } else if (id <= IdJoypadR3) {
            state = self->m_callbacks.inputState(port, id);
        }
    }, [self](const std::string& error) {
        if (self->m_callbacks.error) self->m_callbacks.error(error);
    });
    return state;
}

} // namespace app
