#include "app/platform/FrontendRuntime.hpp"

#include "app/logging/Logger.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <stdexcept>
#include <string>

namespace app {

FrontendRuntime::~FrontendRuntime() {
    if (m_imguiRendererInitialized) ImGui_ImplSDLRenderer3_Shutdown();
    if (m_imguiPlatformInitialized) ImGui_ImplSDL3_Shutdown();
    if (m_imguiContext) ImGui::DestroyContext();
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    if (m_sdlInitialized) SDL_Quit();
}

void FrontendRuntime::initialize(const Settings& settings, const std::string& imguiIniFile) {
    if (m_sdlInitialized) throw std::logic_error("The SDL frontend runtime is already initialized.");
    if (settings.audioDriver == "wasapi") SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "wasapi");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        m_sdlInitialized = true;
        throw std::runtime_error(std::string("Unable to initialize SDL3: ") + SDL_GetError());
    }
    m_sdlInitialized = true;

    SDL_WindowFlags apiFlag{};
    if (settings.videoDriver == "opengl") {
        apiFlag = SDL_WINDOW_OPENGL;
    } else if (settings.videoDriver == "vulkan") {
        apiFlag = SDL_WINDOW_VULKAN;
    } else if (settings.videoDriver != "direct3d12") {
        throw std::invalid_argument("Unsupported video renderer: " + settings.videoDriver);
    }
    m_window = SDL_CreateWindow("Opera SDL3 Frontend", 1100, 760,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | apiFlag);
    if (!m_window) {
        throw std::runtime_error(std::string("Unable to create the SDL window: ") + SDL_GetError());
    }

    m_renderer = SDL_CreateRenderer(m_window, settings.videoDriver.c_str());
    if (!m_renderer) {
        throw std::runtime_error(std::string("Unable to create the ") + settings.videoDriver +
                                 " video renderer: " + SDL_GetError());
    }
    m_vsyncActive = SDL_SetRenderVSync(m_renderer, settings.vsync ? 1 : 0);
    if (!m_vsyncActive) {
        Logger::write(LogLevel::Warning, std::string("VSync could not be configured: ") + SDL_GetError());
    }
    if (settings.fullscreen && !setFullscreen(true, settings.fullscreenMode)) {
        Logger::write(LogLevel::Warning, std::string("Fullscreen could not be enabled: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imguiContext = true;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::GetIO().IniFilename = imguiIniFile.c_str();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer)) {
        throw std::runtime_error("Unable to initialize the Dear ImGui SDL3 platform backend.");
    }
    m_imguiPlatformInitialized = true;
    if (!ImGui_ImplSDLRenderer3_Init(m_renderer)) {
        throw std::runtime_error("Unable to initialize the Dear ImGui SDL renderer backend.");
    }
    m_imguiRendererInitialized = true;
}

bool FrontendRuntime::setFullscreen(const bool fullscreen, const std::string& mode) {
    if (mode != "borderless" && mode != "exclusive") {
        SDL_SetError("Unsupported fullscreen mode: %s", mode.c_str());
        return false;
    }

    const auto restoreWindowed = [this]() {
        if (!m_activeFullscreenMode.empty()) {
            if (!SDL_SetWindowFullscreen(m_window, false) || !SDL_SyncWindow(m_window)) return false;
            if (!SDL_SetWindowFullscreenMode(m_window, nullptr)) return false;
        }
        if (!SDL_SetWindowBordered(m_window, true) ||
            !SDL_SetWindowPosition(m_window, m_windowedX, m_windowedY) ||
            !SDL_SetWindowSize(m_window, m_windowedWidth, m_windowedHeight) ||
            !SDL_SyncWindow(m_window)) {
            return false;
        }
        m_fullscreen = false;
        m_activeFullscreenMode.clear();
        m_hasWindowedBounds = false;
        return true;
    };

    if (!fullscreen) {
        return !m_fullscreen || restoreWindowed();
    }

    if (m_fullscreen && m_activeFullscreenMode == mode) return true;
    if (m_fullscreen && !restoreWindowed()) return false;
    if (!m_hasWindowedBounds) {
        if (!SDL_GetWindowPosition(m_window, &m_windowedX, &m_windowedY) ||
            !SDL_GetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight)) {
            return false;
        }
        m_hasWindowedBounds = true;
    }

    m_activeFullscreenMode = mode;
    bool entered = false;
    if (mode == "borderless") {
        entered = SDL_SetWindowFullscreenMode(m_window, nullptr) &&
            SDL_SetWindowFullscreen(m_window, true) &&
            SDL_SyncWindow(m_window);
    } else {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
        const SDL_DisplayMode* displayMode = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
        entered = displayMode &&
            SDL_SetWindowBordered(m_window, true) &&
            SDL_SetWindowFullscreenMode(m_window, displayMode) &&
            SDL_SetWindowFullscreen(m_window, true) &&
            SDL_SyncWindow(m_window);
    }

    if (!entered) {
        const std::string error = SDL_GetError();
        if (!restoreWindowed()) {
            Logger::write(LogLevel::Error, std::string("Unable to restore the window after fullscreen failed: ") +
                                           SDL_GetError());
        }
        m_fullscreen = false;
        m_activeFullscreenMode.clear();
        m_hasWindowedBounds = false;
        SDL_SetError("%s", error.c_str());
        return false;
    }
    m_fullscreen = true;
    m_activeFullscreenMode = mode;
    return true;
}

bool FrontendRuntime::recreateRendererDeviceObjects() noexcept {
    ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
    return ImGui_ImplSDLRenderer3_CreateDeviceObjects();
}

void FrontendRuntime::processEvent(const SDL_Event& event) const noexcept {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool FrontendRuntime::setVsync(const bool enabled) noexcept {
    m_vsyncActive = SDL_SetRenderVSync(m_renderer, enabled ? 1 : 0);
    return m_vsyncActive;
}

} // namespace app
