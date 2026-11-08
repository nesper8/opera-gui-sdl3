cmake_minimum_required(VERSION 3.24)

project(opera_sdl3_frontend VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The Opera SDL3 frontend currently targets Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Opera x86_64 libretro core requires a 64-bit frontend build.")
endif()

include(FetchContent)

set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3 imgui)

add_executable(opera_frontend
    src/Application.cpp
    src/AudioOutput.cpp
    src/CoreDownloader.cpp
    src/InputManager.cpp
    src/LibretroCore.cpp
    src/Logger.cpp
    src/Settings.cpp
    src/main.cpp
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
)

target_compile_features(opera_frontend PRIVATE cxx_std_20)
target_compile_definitions(opera_frontend PRIVATE SDL_MAIN_HANDLED NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(opera_frontend PRIVATE include "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(opera_frontend PRIVATE SDL3::SDL3 winhttp)
add_custom_command(TARGET opera_frontend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SDL3::SDL3>"
        "$<TARGET_FILE_DIR:opera_frontend>"
)

if(MSVC)
    target_compile_options(opera_frontend PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(opera_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(settings_tests tests/SettingsTests.cpp src/Settings.cpp)
    target_compile_features(settings_tests PRIVATE cxx_std_20)
    target_include_directories(settings_tests PRIVATE include)
    if(MSVC)
        target_compile_options(settings_tests PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(settings_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME settings_tests COMMAND settings_tests)
endif()
