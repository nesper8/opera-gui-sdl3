#include "app/Application.hpp"

#include "app/Logger.hpp"
#include "app/WindowsGpuPreference.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
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

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t byte : value) result.push_back(static_cast<char>(byte));
    return result;
}

bool selectableCombo(const char* label, std::string& value,
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

const char* playerName(const std::size_t player) {
    return player == 0 ? "Player 1" : "Player 2";
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
    for (const char* filename : biosFiles) {
        const auto candidate = directory / filename;
        std::error_code error;
        const bool isFile = std::filesystem::is_regular_file(candidate, error);
        if (error) {
            throw std::filesystem::filesystem_error("Unable to inspect Opera BIOS file",
                                                    candidate, error);
        }
        if (isFile) return candidate;
    }
    return std::nullopt;
}

} // namespace

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
    if (m_imguiRendererInitialized) ImGui_ImplSDLRenderer3_Shutdown();
    if (m_imguiPlatformInitialized) ImGui_ImplSDL3_Shutdown();
    if (m_imguiContext) {
        ImGui::DestroyContext();
    }
    m_core.reset();
    m_audio.reset();
    m_input.reset();
    if (m_frameTexture) SDL_DestroyTexture(m_frameTexture);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    if (m_sdlInitialized) SDL_Quit();
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
        if (m_core && m_core->gameLoaded()) {
            const auto now = Clock::now();
            const auto frameDuration = std::chrono::duration<double>(1.0 / m_core->frameRate());
            if (now >= nextCoreFrame) {
                m_core->run();
                nextCoreFrame = now + std::chrono::duration_cast<Clock::duration>(frameDuration);
            }
        } else {
            nextCoreFrame = Clock::now();
        }
        render();
        if (m_core && m_core->gameLoaded() && !m_vsyncActive) {
            std::this_thread::sleep_until(nextCoreFrame);
        } else if ((!m_core || !m_core->gameLoaded()) && !m_vsyncActive) {
            SDL_Delay(16);
        }
    }
    return 0;
}

void Application::initialize() {
    if (m_settings.audioDriver == "wasapi") SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "wasapi");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        m_sdlInitialized = true;
        throw std::runtime_error(std::string("Unable to initialize SDL3: ") + SDL_GetError());
    }
    m_sdlInitialized = true;

    const SDL_WindowFlags apiFlag = m_settings.videoDriver == "vulkan" ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL;
    m_window = SDL_CreateWindow("Opera SDL3 Frontend", 1100, 760,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | apiFlag);
    if (!m_window) throw std::runtime_error(std::string("Unable to create the SDL window: ") + SDL_GetError());

    const char* driver = m_settings.videoDriver.c_str();
    m_renderer = SDL_CreateRenderer(m_window, driver);
    if (!m_renderer) {
        throw std::runtime_error(std::string("Unable to create the ") + driver +
                                 " video renderer: " + SDL_GetError());
    }
    m_vsyncActive = SDL_SetRenderVSync(m_renderer, m_settings.vsync ? 1 : 0);
    if (!m_vsyncActive) {
        m_settings.vsync = false;
        Logger::write(LogLevel::Warning, std::string("VSync could not be configured: ") + SDL_GetError());
    }
    if (m_settings.fullscreen && !SDL_SetWindowFullscreen(m_window, true)) {
        Logger::write(LogLevel::Warning, std::string("Fullscreen could not be enabled: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imguiContext = true;
    ImGui::GetIO().IniFilename = m_imguiIniFile.c_str();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer)) {
        throw std::runtime_error("Unable to initialize the Dear ImGui SDL3 platform backend.");
    }
    m_imguiPlatformInitialized = true;
    if (!ImGui_ImplSDLRenderer3_Init(m_renderer)) {
        throw std::runtime_error("Unable to initialize the Dear ImGui SDL renderer backend.");
    }
    m_imguiRendererInitialized = true;

    m_input = std::make_unique<InputManager>(m_settings);
    m_input->refresh();
    try {
        m_audioDevices = AudioOutput::playbackDevices();
    } catch (const std::exception& error) {
        setError(error.what());
    }
    m_audio = std::make_unique<AudioOutput>(m_settings);
    if (std::filesystem::exists(m_coreDirectory / "opera_libretro.dll")) {
        try {
            loadCore();
        } catch (const std::exception& error) {
            setError(error.what());
        }
    }
}

void Application::handleEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    const bool bindingWasActive = m_captureBinding.has_value();
    if (m_captureBinding && event.type == SDL_EVENT_KEY_DOWN &&
        event.key.scancode == SDL_SCANCODE_ESCAPE) {
        m_captureBinding.reset();
        m_status = "Binding cancelled.";
    } else if (m_captureBinding && !m_captureBinding->gamepad &&
               event.type == SDL_EVENT_KEY_DOWN) {
        m_input->setKeyboardBinding(m_captureBinding->player, m_captureBinding->button,
                                    event.key.scancode);
        m_captureBinding.reset();
        saveSettings();
        m_status = "Keyboard binding updated.";
    } else if (m_captureBinding && m_captureBinding->gamepad &&
               event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
               event.gbutton.which == m_input->selectedGamepadId(m_captureBinding->player)) {
        m_input->setGamepadButtonBinding(m_captureBinding->player, m_captureBinding->button,
            static_cast<SDL_GamepadButton>(event.gbutton.button));
        m_captureBinding.reset();
        saveSettings();
        m_status = "Gamepad binding updated.";
    } else if (m_captureBinding && m_captureBinding->gamepad &&
               event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               event.gaxis.which == m_input->selectedGamepadId(m_captureBinding->player) &&
               event.gaxis.value > 16000 &&
               (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        m_input->setGamepadAxisBinding(m_captureBinding->player, m_captureBinding->button,
                                       static_cast<SDL_GamepadAxis>(event.gaxis.axis));
        m_captureBinding.reset();
        saveSettings();
        m_status = "Gamepad trigger binding updated.";
    }
    if (event.type == SDL_EVENT_QUIT) {
        m_running = false;
    } else if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        try {
            m_input->refresh();
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
        if (!SDL_SetWindowFullscreen(m_window, m_settings.fullscreen)) setError(SDL_GetError());
        saveSettings();
    } else if (!bindingWasActive && event.type == SDL_EVENT_KEY_DOWN &&
               event.key.key == SDLK_F1 && !event.key.repeat) {
        if (m_core) m_core->reset();
    }
}

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
    if (m_core && m_core->gameLoaded()) {
        ImGui::SameLine();
        if (ImGui::Button("Reset (F1)")) m_core->reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide menu (F2)")) {
        m_uiVisible = false;
        m_captureBinding.reset();
    }
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
    if (ImGui::BeginCombo("Player", playerName(m_selectedPlayer))) {
        for (std::size_t player = 0; player < 2; ++player) {
            const bool selected = m_selectedPlayer == player;
            if (ImGui::Selectable(playerName(player), selected)) m_selectedPlayer = player;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    std::string selected = m_settings.gamepadGuids[m_selectedPlayer];
    bool changed = false;
    if (ImGui::BeginCombo("Device", selected.empty() ? "Keyboard" : selected.c_str())) {
        if (ImGui::Selectable("Keyboard", selected.empty())) {
            selected.clear();
            changed = true;
        }
        for (const auto& device : m_input->gamepads()) {
            const std::string label = device.name + " (" + device.key + ")";
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
            m_input->refresh();
        } catch (const std::exception& error) {
            setError(error.what());
        }
        saveSettings();
    }

    ImGui::SeparatorText("3DO controls");
    ImGui::TextUnformatted("Click a binding, then press a key or controller button. Esc cancels.");
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

            ImGui::TableSetColumnIndex(1);
            const auto key = static_cast<SDL_Scancode>(m_settings.keyboardBindings[m_selectedPlayer][button]);
            const char* keyName = SDL_GetScancodeName(key);
            const bool capturingKey = m_captureBinding && !m_captureBinding->gamepad &&
                m_captureBinding->player == m_selectedPlayer && m_captureBinding->button == button;
            if (ImGui::Button(capturingKey ? "Press key..." :
                              (keyName && *keyName ? keyName : "Unbound"), ImVec2(-1, 0))) {
                m_captureBinding = BindingCapture{m_selectedPlayer, button, false};
            }

            ImGui::TableSetColumnIndex(2);
            const SDL_JoystickID gamepadId = m_input->selectedGamepadId(m_selectedPlayer);
            const bool capturingGamepad = m_captureBinding && m_captureBinding->gamepad &&
                m_captureBinding->player == m_selectedPlayer && m_captureBinding->button == button;
            const std::string bindingName = m_input->gamepadBindingName(m_selectedPlayer, button);
            ImGui::BeginDisabled(gamepadId == 0);
            if (ImGui::Button(capturingGamepad ? "Press button..." : bindingName.c_str(),
                              ImVec2(-1, 0))) {
                m_captureBinding = BindingCapture{m_selectedPlayer, button, true};
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (m_input->selectedGamepadId(m_selectedPlayer) == 0) {
        ImGui::TextWrapped("Select a connected gamepad above to remap its controls.");
    }
}

void Application::drawVideoSettings() {
    bool changed = selectableCombo("Video driver", m_settings.videoDriver,
        {{"opengl", "OpenGL"}, {"vulkan", "Vulkan"}});
    const std::string previousGpuPreference = m_settings.gpuPreference;
    const bool gpuPreferenceChanged = selectableCombo("GPU preference", m_settings.gpuPreference,
        {{"system", "System default"}, {"power_saving", "Power saving GPU"},
         {"high_performance", "High performance GPU"}});
    if (gpuPreferenceChanged) {
        try {
            setWindowsGpuPreference(m_settings.gpuPreference);
            saveSettings();
            m_status = "GPU preference saved. Restart the application for it to take effect.";
        } catch (const std::exception& error) {
            m_settings.gpuPreference = previousGpuPreference;
            setError(std::string("Unable to apply Windows GPU preference: ") + error.what());
        }
    }
    changed |= selectableCombo("Scaling", m_settings.scaleMode,
        {{"nearest", "Nearest (pixel sharp)"}, {"linear", "Linear"}});
    changed |= selectableCombo("Shader preset", m_settings.shaderPreset,
        {{"none", "None"}, {"crt", "CRT scanlines"}});
    changed |= ImGui::Checkbox("Vertical sync", &m_settings.vsync);

    if (ImGui::Checkbox("Fullscreen (F11)", &m_settings.fullscreen)) {
        if (!SDL_SetWindowFullscreen(m_window, m_settings.fullscreen)) setError(SDL_GetError());
        changed = true;
    }
    if (changed) {
        saveSettings();
        m_status = "Restart the application to apply a video driver change.";
        if (m_frameTexture) SDL_SetTextureScaleMode(m_frameTexture,
            m_settings.scaleMode == "nearest" ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        m_vsyncActive = SDL_SetRenderVSync(m_renderer, m_settings.vsync ? 1 : 0);
        if (!m_vsyncActive) {
            m_settings.vsync = false;
            setError(std::string("VSync change failed: ") + SDL_GetError());
            saveSettings();
        }
    }
    ImGui::TextWrapped("OpenGL or Vulkan is selected through SDL3's renderer backend. CRT scanlines are applied at presentation.");
    ImGui::TextWrapped("GPU preference is stored in Windows per-app graphics settings and requires a restart. Windows or vendor driver settings may override it.");
}

void Application::drawAudioSettings() {
    const bool driverChanged = selectableCombo("Audio driver", m_settings.audioDriver,
        {{"wasapi", "WASAPI"}, {"default", "SDL default"}});
    bool outputChanged = false;
    const std::string previousDevice = m_settings.audioDevice;
    {
        const char* selectedDevice = m_settings.audioDevice.empty()
            ? "System default" : m_settings.audioDevice.c_str();
        if (ImGui::BeginCombo("Output device", selectedDevice)) {
            if (ImGui::Selectable("System default", m_settings.audioDevice.empty())) {
                m_settings.audioDevice.clear();
                outputChanged = true;
            }
            for (const AudioDeviceInfo& device : m_audioDevices) {
                const bool selected = m_settings.audioDevice == device.name;
                if (ImGui::Selectable(device.name.c_str(), selected)) {
                    m_settings.audioDevice = device.name;
                    outputChanged = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    const int previousLatency = m_settings.audioLatencyMs;
    const bool latencyChanged = ImGui::SliderInt(
        "Audio latency (ms)", &m_settings.audioLatencyMs, 16, 200);
    if (latencyChanged) {
        ImGui::SameLine();
        ImGui::TextDisabled("(lower latency, higher stutter risk)");
    }

    if (ImGui::SliderFloat("Output gain", &m_settings.audioGain, 0.0F, 1.0F, "%.2f")) {
        if (m_audio) m_audio->setGain(m_settings.audioGain);
        saveSettings();
    }

    if (outputChanged || latencyChanged) {
        if (m_core && m_core->gameLoaded()) {
            try {
                m_audio->configure(m_core->sampleRate());
            } catch (const std::exception& error) {
                m_settings.audioDevice = previousDevice;
                m_settings.audioLatencyMs = previousLatency;
                setError(std::string("Unable to reconfigure audio output: ") + error.what());
            }
        }
        saveSettings();
        if (m_error.empty()) m_status = "Audio output settings applied.";
    }
    if (driverChanged) {
        saveSettings();
        m_status = "Restart the application to apply the audio driver change.";
    }
    ImGui::TextWrapped("Playback rate is kept at the core's native speed. Output gain is capped at unity to avoid introducing clipping.");
    ImGui::TextWrapped("The requested device buffer size is a hint; SDL and the audio driver may choose a different effective latency.");
}

void Application::drawPerformanceSettings() {
    if (ImGui::Checkbox("Show FPS counter (F3)", &m_settings.showFpsCounter)) {
        saveSettings();
    }
    ImGui::TextWrapped("The counter reports the frontend's rendered frames per second.");
    ImGui::TextUnformatted("F3 toggles the counter at any time.");
}

void Application::drawCoreSettings() {
    const std::string systemDirectory = pathToUtf8(m_systemDirectory);
    const std::string biosDirectory = pathToUtf8(m_biosDirectory);
    const std::string saveDirectory = pathToUtf8(m_saveDirectory);
    ImGui::TextWrapped("Optional BIOS directory: %s", biosDirectory.c_str());
    if (m_detectedBios) {
        const std::string biosName = pathToUtf8(m_detectedBios->filename());
        ImGui::TextWrapped("BIOS file found: %s", biosName.c_str());
    } else {
        ImGui::TextUnformatted("BIOS file found: none; using the user system directory.");
    }
    if (ImGui::Checkbox("Use detected BIOS when available", &m_settings.useDetectedBios)) {
        saveSettings();
        m_status = "Restart the application to apply the BIOS setting.";
    }
    ImGui::TextWrapped("System files: %s", systemDirectory.c_str());
    ImGui::TextWrapped("Save files: %s", saveDirectory.c_str());
    if (!m_core) {
        ImGui::TextWrapped("Install opera_libretro.dll to configure the Opera core.");
        return;
    }
    ImGui::Text("%s", m_coreName.c_str());
    if (!m_core->gameLoaded()) ImGui::TextUnformatted("Load a game to begin emulation.");
    for (const auto& option : m_core->options()) {
        if (option.values.empty()) continue;
        int selected = 0;
        const auto current = std::find(option.values.begin(), option.values.end(), option.currentValue);
        if (current != option.values.end()) selected = static_cast<int>(std::distance(option.values.begin(), current));
        std::vector<const char*> labels;
        labels.reserve(option.values.size());
        for (const auto& value : option.values) labels.push_back(value.c_str());
        ImGui::PushID(option.key.c_str());
        if (ImGui::Combo(option.description.c_str(), &selected, labels.data(), static_cast<int>(labels.size()))) {
            try {
                m_core->setOption(option.key, option.values[static_cast<std::size_t>(selected)]);
                m_settings.coreOptions[option.key] = option.values[static_cast<std::size_t>(selected)];
                saveSettings();
            } catch (const std::exception& error) {
                setError(error.what());
            }
        }
        ImGui::PopID();
    }
}

void Application::render() {
    const auto now = std::chrono::steady_clock::now();
    ++m_fpsFrameCount;
    if (m_fpsLastUpdate == std::chrono::steady_clock::time_point{}) {
        m_fpsLastUpdate = now;
    } else {
        const std::chrono::duration<double> elapsed = now - m_fpsLastUpdate;
        if (elapsed.count() >= 0.5) {
            m_fpsValue = static_cast<double>(m_fpsFrameCount) / elapsed.count();
            m_fpsFrameCount = 0;
            m_fpsLastUpdate = now;
        }
    }
    if (m_uiVisible) m_menuHintStart = {};
    else if (m_menuHintStart == std::chrono::steady_clock::time_point{}) m_menuHintStart = now;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (m_uiVisible) {
        drawInterface();
    } else {
        const float hiddenSeconds = std::chrono::duration<float>(now - m_menuHintStart).count();
        constexpr float fadeStartSeconds = 5.0F;
        constexpr float fadeDurationSeconds = 1.0F;
        if (hiddenSeconds < fadeStartSeconds + fadeDurationSeconds) {
            const float alpha = hiddenSeconds <= fadeStartSeconds ? 1.0F :
                1.0F - (hiddenSeconds - fadeStartSeconds) / fadeDurationSeconds;
            ImGui::GetForegroundDrawList()->AddText(ImVec2(12, 12),
                IM_COL32(220, 220, 220, static_cast<int>(180.0F * alpha)), "F2: Show menu");
        }
    }
    if (m_settings.showFpsCounter) {
        char fpsText[32]{};
        std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", m_fpsValue);
        const ImVec2 textSize = ImGui::CalcTextSize(fpsText);
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        const ImVec2 position(displaySize.x - textSize.x - 24.0F, 12.0F);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(ImVec2(position.x - 6.0F, position.y - 4.0F),
                                ImVec2(position.x + textSize.x + 6.0F,
                                       position.y + textSize.y + 4.0F),
                                IM_COL32(0, 0, 0, 150), 4.0F);
        drawList->AddText(position, IM_COL32(235, 235, 235, 230), fpsText);
    }
    ImGui::Render();

    SDL_SetRenderDrawColor(m_renderer, 12, 14, 18, 255);
    SDL_RenderClear(m_renderer);
    drawVideoFrame();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_renderer);
    SDL_RenderPresent(m_renderer);
}

void Application::openGameDialog() {
    static constexpr SDL_DialogFileFilter filters[]{
        {"3DO games", "iso;cue;bin"},
        {"ISO disc images", "iso"},
        {"BIN/CUE disc images", "cue;bin"}
    };
    SDL_ShowOpenFileDialog(&Application::fileDialogCallback, this, m_window,
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

void Application::loadGame(const std::filesystem::path& path) {
    if (!m_core) loadCore();
    m_gamePath.clear();
    m_frame = {};
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".iso" && extension != ".cue" && extension != ".bin") {
        throw std::invalid_argument("Choose an ISO, CUE, or BIN game image.");
    }
    m_core->loadGame(path);
    m_audio->configure(m_core->sampleRate());
    m_gamePath = path.string();
    m_status = "Running " + path.filename().string() + ".";
    m_error.clear();
    m_uiVisible = false;
    m_captureBinding.reset();
}

void Application::loadCore() {
    auto core = std::make_unique<LibretroCore>(m_systemDirectory, m_saveDirectory, CoreCallbacks{
        [this](const void* data, const unsigned width, const unsigned height, const std::size_t pitch) {
            onVideo(data, width, height, pitch);
        },
        [this](const unsigned format) { m_pixelFormat = format; },
        [this](const std::int16_t* data, const std::size_t frames) { m_audio->submit(data, frames); },
        [] { SDL_PumpEvents(); },
        [this](const unsigned player, const unsigned button) { return m_input->buttonState(player, button); },
        [] {},
        [this](const std::string& error) { setError(error); }
    });
    core->load(m_coreDirectory / "opera_libretro.dll");
    const std::string coreName = core->coreName();
    for (const auto& option : core->options()) {
        const auto saved = m_settings.coreOptions.find(option.key);
        if (saved != m_settings.coreOptions.end() &&
            std::find(option.values.begin(), option.values.end(), saved->second) != option.values.end()) {
            core->setOption(option.key, saved->second);
        } else if (!option.values.empty()) {
            m_settings.coreOptions[option.key] = option.currentValue;
        }
    }
    m_core = std::move(core);
    m_coreName = coreName;
    m_status = "Opera core loaded. Choose an ISO, CUE, or BIN image.";
    m_error.clear();
}

void Application::checkDownload() {
    if (!m_download.valid() || m_download.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        const auto installed = m_download.get();
        m_status = "Opera core installed to " + installed.string();
        m_error.clear();
        loadCore();
    } catch (const std::exception& error) {
        setError(error.what());
    }
}

void Application::requestCoreDownload() {
    if (m_download.valid() && m_download.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    if (m_core) {
        m_core.reset();
        m_coreName.clear();
        m_gamePath.clear();
        m_frame = {};
    }
    m_status = "Downloading and extracting the Opera core...";
    m_error.clear();
    m_download = std::async(std::launch::async, [directory = m_coreDirectory] {
        return CoreDownloader{}.downloadAndExtract(directory);
    });
}

void Application::saveSettings() {
    try {
        m_settings.save(m_settingsFile);
    } catch (const std::exception& error) {
        setError(std::string("Unable to save settings: ") + error.what());
    }
}

void Application::setError(const std::string& message) {
    m_error = message;
    m_status = "An error occurred.";
    Logger::write(LogLevel::Error, message);
}

void Application::onVideo(const void* data, const unsigned width, const unsigned height,
                          const std::size_t pitch) {
    if (!data) return;
    constexpr unsigned MaxDimension = 4096;
    if (width == 0 || height == 0 || width > MaxDimension || height > MaxDimension) {
        throw std::runtime_error("Opera core reported invalid video dimensions.");
    }
    if (static_cast<std::size_t>(width) * height > 16U * 1024U * 1024U) {
        throw std::runtime_error("Opera core reported an oversized video frame.");
    }
    unsigned bytesPerPixel{};
    switch (m_pixelFormat) {
    case libretro::Pixel0Rgb1555:
    case libretro::PixelRgb565:
        bytesPerPixel = 2;
        break;
    case libretro::PixelXrgb8888:
        bytesPerPixel = 4;
        break;
    default:
        throw std::runtime_error("Opera core selected an unsupported video pixel format.");
    }
    if (pitch < static_cast<std::size_t>(width) * bytesPerPixel) {
        throw std::runtime_error("Opera core reported an invalid video frame pitch.");
    }
    if (pitch > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("Opera core reported a video pitch that overflows the frame buffer.");
    }
    m_frame.width = width;
    m_frame.height = height;
    m_frame.pixelFormat = m_pixelFormat;
    const std::size_t rowSize = static_cast<std::size_t>(width) * bytesPerPixel;
    m_frame.pixels.resize(rowSize * height);
    const auto* source = static_cast<const std::uint8_t*>(data);
    for (unsigned y = 0; y < height; ++y) {
        std::memcpy(m_frame.pixels.data() + static_cast<std::size_t>(y) * rowSize,
                    source + static_cast<std::size_t>(y) * pitch, rowSize);
    }
    m_frame.dirty = true;
}

void Application::drawVideoFrame() {
    if (m_frame.width == 0 || m_frame.height == 0) return;
    SDL_PixelFormat pixelFormat{};
    unsigned bytesPerPixel{};
    switch (m_frame.pixelFormat) {
    case libretro::Pixel0Rgb1555:
        pixelFormat = SDL_PIXELFORMAT_XRGB1555;
        bytesPerPixel = 2;
        break;
    case libretro::PixelRgb565:
        pixelFormat = SDL_PIXELFORMAT_RGB565;
        bytesPerPixel = 2;
        break;
    case libretro::PixelXrgb8888:
        pixelFormat = SDL_PIXELFORMAT_XRGB8888;
        bytesPerPixel = 4;
        break;
    default:
        setError("Opera core selected an unsupported video pixel format.");
        return;
    }
    if (!m_frameTexture || m_frameTextureWidth != static_cast<int>(m_frame.width) ||
        m_frameTextureHeight != static_cast<int>(m_frame.height) ||
        m_frameTextureFormat != m_frame.pixelFormat) {
        if (m_frameTexture) SDL_DestroyTexture(m_frameTexture);
        m_frameTexture = SDL_CreateTexture(m_renderer, pixelFormat,
            SDL_TEXTUREACCESS_STREAMING, static_cast<int>(m_frame.width), static_cast<int>(m_frame.height));
        if (!m_frameTexture) {
            setError(std::string("Unable to create the game video texture: ") + SDL_GetError());
            return;
        }
        m_frameTextureWidth = static_cast<int>(m_frame.width);
        m_frameTextureHeight = static_cast<int>(m_frame.height);
        m_frameTextureFormat = m_frame.pixelFormat;
        SDL_SetTextureScaleMode(m_frameTexture,
            m_settings.scaleMode == "nearest" ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    }
    if (m_frame.dirty) {
        if (!SDL_UpdateTexture(m_frameTexture, nullptr, m_frame.pixels.data(),
                               static_cast<int>(m_frame.width * bytesPerPixel))) {
            setError(std::string("Unable to upload the game video frame: ") + SDL_GetError());
            return;
        }
        m_frame.dirty = false;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetRenderOutputSize(m_renderer, &windowWidth, &windowHeight);
    if (windowWidth <= 0 || windowHeight <= 0) return;
    const float aspect = static_cast<float>(m_frame.width) / static_cast<float>(m_frame.height);
    float width = static_cast<float>(windowWidth);
    float height = width / aspect;
    if (height > static_cast<float>(windowHeight)) {
        height = static_cast<float>(windowHeight);
        width = height * aspect;
    }
    const SDL_FRect destination{
        (static_cast<float>(windowWidth) - width) * 0.5F,
        (static_cast<float>(windowHeight) - height) * 0.5F,
        width,
        height
    };
    SDL_RenderTexture(m_renderer, m_frameTexture, nullptr, &destination);

    if (m_settings.shaderPreset == "crt") {
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 64);
        const float spacing = std::max(2.0F, height / 240.0F);
        for (float y = destination.y; y < destination.y + destination.h; y += spacing) {
            const SDL_FRect line{destination.x, y, destination.w, std::max(1.0F, spacing * 0.35F)};
            SDL_RenderFillRect(m_renderer, &line);
        }
        SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    }
}

} // namespace app
