#include "app/application/Application.hpp"

#include "app/audio/AudioOutput.hpp"
#include "app/core/CoreDownloader.hpp"
#include "app/core/LibretroCore.hpp"
#include "app/input/InputManager.hpp"
#include "app/logging/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace app {
void Application::loadGame(const std::filesystem::path& path) {
    if (!m_session.core()) loadCore();
    m_gamePath.clear();
    m_frame = {};
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".iso" && extension != ".cue" && extension != ".bin") {
        throw std::invalid_argument("Choose an ISO, CUE, or BIN game image.");
    }
    m_session.loadGame(path);
    m_gamePath = path.string();
    m_status = "Running " + path.filename().string() + ".";
    m_error.clear();
    m_uiVisible = false;
    m_captureBinding.reset();
}

void Application::loadCore() {
    CoreCallbacks callbacks{
        [this](const void* data, const unsigned width, const unsigned height, const std::size_t pitch) {
            onVideo(data, width, height, pitch);
        },
        [this](const unsigned format) { m_pixelFormat = format; },
        [this](const std::int16_t* data, const std::size_t frames) { m_session.audio()->submit(data, frames); },
        [] { SDL_PumpEvents(); },
        [this](const unsigned player, const unsigned button) { return m_session.input()->buttonState(player, button); },
        [] {},
        [this](const std::string& error) { setError(error); }
    };
    m_session.loadCore(m_coreDirectory / "opera_libretro.dll", m_systemDirectory,
                       m_saveDirectory, m_settings, std::move(callbacks));
    m_coreName = m_session.core()->coreName();
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
    if (m_session.core()) {
        m_session.unloadCore();
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

} // namespace app
