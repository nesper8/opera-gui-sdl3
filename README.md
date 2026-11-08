# Opera SDL3 Frontend

A Windows desktop frontend that loads only the Opera 3DO libretro core. It does
not launch RetroArch.

## Features

- Downloads the official `opera_libretro.dll` ZIP from the libretro buildbot,
  extracts it to the application's `cores` directory, and installs the DLL
  only after extraction succeeds.
- Loads the DLL through the libretro API and exposes version-1 core options
  with legacy option fallback in the settings window, with a control to restore
  the Opera core's option defaults.
- Provides a section-specific revert-to-defaults control on each settings tab.
- Opens ISO, BIN/CUE, and CHD disc images using SDL's native file dialog.
- Routes the two emulated joypads independently to an SDL gamepad (including
  XInput controllers) or remappable keyboard controls. Bindings are captured
  from connected controllers or the keyboard; analog stick directions can be
  bound independently to digital controls such as the D-pad. Bindings are
  displayed in 3DO order. Left-click a binding to replace it, right-click to
  add another input to the same control, and middle-click to remove a binding:
  D-pad, A/B/C, L/R, Select, and Start. Identical gamepads are distinguished
  by their enumeration order.
- Automatically hides the settings window after a game loads; close or hide it
  at any time and press F2 to show it again. The cursor hides after five seconds
  without mouse movement while the menu is hidden, and reappears when the mouse
  moves. The menu includes a Quit button.
- Shows the hidden-menu hint for five seconds before fading it out, and provides
  an optional rendered-FPS counter that can be enabled in the Performance tab
  or toggled at any time with F3.
- Selects SDL's OpenGL, Vulkan, or Direct3D 12 renderer, nearest/linear scaling, VSync,
  user-selectable borderless windowed or exclusive fullscreen, and a built-in
  CRT scanline presentation preset. F11 toggles between windowed and the
  selected fullscreen mode. On Windows,
  the GPU preference can be set to system default, power-saving, or
  high-performance; the choice is stored as a per-application Windows graphics
  preference and takes effect after restarting the frontend.
- Exposes the Opera core's 640x480 high-resolution framebuffer option in Video
  settings when supported by the loaded core.
- Uses SDL audio with the SDL default backend selected by default, optional
  WASAPI selection, selectable output device, 40 ms default audio latency
  (configurable in settings), and
  adjustable output gain. Playback stays at the core's native rate, and gain
  is capped at unity to avoid frontend-induced clipping. Audio latency is a
  requested SDL buffer size; the audio backend may choose a different effective value.
- Detects supported 3DO BIOS filenames in the application's `bios` directory
  and uses that directory as the core system directory when enabled. Detection
  is optional; the frontend launches without proprietary BIOS files and uses
  the existing user system directory automatically when no BIOS is found.
- Persists settings under the current user's SDL preferences directory and
  records diagnostics in `opera-frontend.log`.

The initial CRT preset is a scanline overlay, not a programmable shader chain;
loading arbitrary RetroArch `.glsl`, `.slang`, or `.preset` shaders is not yet
supported.
Any BIOS files must be legally obtained by the user. When BIOS detection is
enabled, place a supported BIOS file in `bios` beside the application; the Core
tab shows the detected file and active system directory. Changing this option
requires restarting the app.

## Build

Requirements: Windows 10 or later, CMake 3.24+, Git, and a C++20 compiler
(MSVC). CMake downloads SDL3 and Dear ImGui at configure time. The release
workflow uses Ninja with the x64 MSVC environment.

```powershell
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The standard tests run offline. To additionally test downloading and extracting
the current Opera core, configure with
`-DOPERA_ENABLE_DOWNLOAD_INTEGRATION_TEST=ON`; that test requires network access
to the libretro buildbot.

Run `build/opera_frontend.exe` (the build copies `SDL3.dll` beside it), download
the core from the launch window, then open an ISO, BIN/CUE, or CHD game image. The
Core tab shows the system and save directories. BIN/CUE sets should be opened
through their `.cue` file.
Changing the video or audio backend requires restarting the application.

## Releases

Pushing a version tag such as `v0.1.0` builds and tests the Windows x64 frontend,
packages `opera_frontend.exe`, `SDL3.dll`, and this README into a ZIP, and
publishes it as a GitHub release. To create a tagged release automatically,
run **Build and release** from the Actions tab and enter a semantic version
without the leading `v` (for example, `0.1.1`). The workflow creates the tag
from the selected commit, then builds and publishes the release.

## Layout

```text
.
├── .github/
│   └── workflows/
│       └── release.yml
├── include/
│   └── app/
│       ├── application/
│       │   └── Application.hpp
│       ├── audio/
│       │   └── AudioOutput.hpp
│       ├── core/
│       │   ├── CoreDownloader.hpp
│       │   ├── LibretroAbi.hpp
│       │   └── LibretroCore.hpp
│       ├── input/
│       │   └── InputManager.hpp
│       ├── logging/
│       │   └── Logger.hpp
│       ├── platform/
│       │   ├── FrontendRuntime.hpp
│       │   └── WindowsGpuPreference.hpp
│       ├── session/
│       │   └── GameSession.hpp
│       └── settings/
│           ├── Settings.hpp
│           └── SettingsWidgets.hpp
├── src/
│   ├── application/
│   │   ├── Application.cpp
│   │   ├── ApplicationCore.cpp
│   │   └── ApplicationRendering.cpp
│   ├── audio/
│   │   └── AudioOutput.cpp
│   ├── core/
│   │   ├── CoreDownloader.cpp
│   │   └── LibretroCore.cpp
│   ├── input/
│   │   └── InputManager.cpp
│   ├── logging/
│   │   └── Logger.cpp
│   ├── platform/
│   │   ├── FrontendRuntime.cpp
│   │   └── WindowsGpuPreference.cpp
│   ├── session/
│   │   └── GameSession.cpp
│   ├── settings/
│   │   ├── ApplicationAudioSettings.cpp
│   │   ├── ApplicationCoreSettings.cpp
│   │   ├── ApplicationInputSettings.cpp
│   │   ├── ApplicationSettings.cpp
│   │   ├── ApplicationVideoSettings.cpp
│   │   └── Settings.cpp
│   └── main.cpp
└── tests/
    ├── CoreDownloaderTests.cpp
    └── SettingsTests.cpp
```
