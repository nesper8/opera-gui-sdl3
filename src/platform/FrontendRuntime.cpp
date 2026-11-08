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

    const SDL_WindowFlags apiFlag =
        settings.videoDriver == "vulkan" ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL;
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
    if (settings.fullscreen && !setFullscreen(true)) {
        Logger::write(LogLevel::Warning, std::string("Fullscreen could not be enabled: ") + SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_imguiContext = true;
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

bool FrontendRuntime::setFullscreen(const bool fullscreen) noexcept {
    return SDL_SetWindowFullscreenMode(m_window, nullptr) &&
        SDL_SetWindowFullscreen(m_window, fullscreen);
}

void FrontendRuntime::processEvent(const SDL_Event& event) const noexcept {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

bool FrontendRuntime::setVsync(const bool enabled) noexcept {
    m_vsyncActive = SDL_SetRenderVSync(m_renderer, enabled ? 1 : 0);
    return m_vsyncActive;
}

} // namespace app
