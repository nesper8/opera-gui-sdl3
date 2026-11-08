#include "app/application/Application.hpp"

#include "app/core/LibretroCore.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace app {
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

    SDL_SetRenderDrawColor(m_runtime.renderer(), 12, 14, 18, 255);
    SDL_RenderClear(m_runtime.renderer());
    drawVideoFrame();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), m_runtime.renderer());
    SDL_RenderPresent(m_runtime.renderer());
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
        m_frameTexture = SDL_CreateTexture(m_runtime.renderer(), pixelFormat,
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
    SDL_GetRenderOutputSize(m_runtime.renderer(), &windowWidth, &windowHeight);
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
    SDL_RenderTexture(m_runtime.renderer(), m_frameTexture, nullptr, &destination);

    if (m_settings.shaderPreset == "crt") {
        SDL_SetRenderDrawBlendMode(m_runtime.renderer(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_runtime.renderer(), 0, 0, 0, 64);
        const float spacing = std::max(2.0F, height / 240.0F);
        for (float y = destination.y; y < destination.y + destination.h; y += spacing) {
            const SDL_FRect line{destination.x, y, destination.w, std::max(1.0F, spacing * 0.35F)};
            SDL_RenderFillRect(m_runtime.renderer(), &line);
        }
        SDL_SetRenderDrawColor(m_runtime.renderer(), 255, 255, 255, 255);
    }
}

} // namespace app
