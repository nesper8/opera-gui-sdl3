#pragma once

#include "app/AudioOutput.hpp"
#include "app/CoreDownloader.hpp"
#include "app/InputManager.hpp"
#include "app/LibretroCore.hpp"
#include "app/Settings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

class Application final {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    struct VideoFrame final {
        unsigned width{};
        unsigned height{};
        unsigned pixelFormat{};
        std::vector<std::uint8_t> pixels;
        bool dirty{};
    };

    struct BindingCapture final {
        std::size_t player{};
        std::size_t button{};
        bool gamepad{};
    };

    struct PadControl final {
        const char* name;
        unsigned retroId;
    };

    static void fileDialogCallback(void* userdata, const char* const* filelist, int filter);
    void initialize();
    void handleEvent(const SDL_Event& event);
    void drawInterface();
    void drawInputSettings();
    void drawVideoSettings();
    void drawAudioSettings();
    void drawPerformanceSettings();
    void drawCoreSettings();
    void render();
    void openGameDialog();
    void loadGame(const std::filesystem::path& path);
    void loadCore();
    void checkDownload();
    void requestCoreDownload();
    void saveSettings();
    void setError(const std::string& message);
    void onVideo(const void* data, unsigned width, unsigned height, std::size_t pitch);
    void drawVideoFrame();
    static const std::array<PadControl, PadButtonCount>& padControls();

    std::filesystem::path m_baseDirectory;
    std::filesystem::path m_preferencesDirectory;
    std::filesystem::path m_settingsFile;
    std::filesystem::path m_coreDirectory;
    std::filesystem::path m_systemDirectory;
    std::filesystem::path m_saveDirectory;
    std::string m_imguiIniFile;
    Settings m_settings;
    SDL_Window* m_window{};
    SDL_Renderer* m_renderer{};
    SDL_Texture* m_frameTexture{};
    int m_frameTextureWidth{};
    int m_frameTextureHeight{};
    unsigned m_frameTextureFormat{};
    unsigned m_pixelFormat{};
    bool m_running{true};
    bool m_sdlInitialized{};
    bool m_vsyncActive{};
    bool m_uiVisible{true};
    std::chrono::steady_clock::time_point m_menuHintStart{};
    std::chrono::steady_clock::time_point m_fpsLastUpdate{};
    std::size_t m_fpsFrameCount{};
    double m_fpsValue{};
    bool m_imguiContext{};
    bool m_imguiPlatformInitialized{};
    bool m_imguiRendererInitialized{};
    std::string m_status = "Download the Opera core to get started.";
    std::string m_error;
    std::string m_coreName;
    std::string m_gamePath;
    VideoFrame m_frame;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<AudioOutput> m_audio;
    std::unique_ptr<LibretroCore> m_core;
    std::future<std::filesystem::path> m_download;
    std::optional<BindingCapture> m_captureBinding;
    std::optional<std::filesystem::path> m_pendingGame;
    std::size_t m_selectedPlayer{};
};

} // namespace app
