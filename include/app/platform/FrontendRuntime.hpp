#pragma once

#include "app/settings/Settings.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace app {

class FrontendRuntime final {
public:
    FrontendRuntime() = default;
    ~FrontendRuntime();

    FrontendRuntime(const FrontendRuntime&) = delete;
    FrontendRuntime& operator=(const FrontendRuntime&) = delete;

    void initialize(const Settings& settings, const std::string& imguiIniFile);
    void processEvent(const SDL_Event& event) const noexcept;
    bool setFullscreen(bool fullscreen) noexcept;
    bool setVsync(bool enabled) noexcept;
    bool vsyncActive() const noexcept { return m_vsyncActive; }
    SDL_Window* window() const noexcept { return m_window; }
    SDL_Renderer* renderer() const noexcept { return m_renderer; }

private:
    SDL_Window* m_window{};
    SDL_Renderer* m_renderer{};
    bool m_sdlInitialized{};
    bool m_vsyncActive{};
    bool m_imguiContext{};
    bool m_imguiPlatformInitialized{};
    bool m_imguiRendererInitialized{};
};

} // namespace app
