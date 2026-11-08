# Opera SDL3 Frontend

A Windows desktop frontend that loads only the Opera 3DO libretro core. It does
not launch RetroArch.

## Features

- Downloads the official `opera_libretro.dll` ZIP from the libretro buildbot,
  extracts it to the application's `cores` directory, and installs the DLL
  only after extraction succeeds.
- Loads the DLL through the libretro API and exposes version-1 core options
  with legacy option fallback in the settings window.
- Opens CHD, ISO, CUE, and BIN images using SDL's native file dialog.
- Routes the two emulated joypads independently to an SDL gamepad (including
  XInput controllers) or remappable keyboard controls. Bindings are captured
  from connected controllers or the keyboard and displayed in 3DO order:
  D-pad, A/B/C, L/R, Select, and Start. Identical gamepads are distinguished
  by their enumeration order.
- Automatically hides the settings window after a game loads; close or hide it
  at any time and press F2 to show it again.
- Shows the hidden-menu hint for five seconds before fading it out, and provides
  an optional rendered-FPS counter that can be enabled in the Performance tab
  or toggled at any time with F3.
- Selects SDL's OpenGL or Vulkan renderer, nearest/linear scaling, VSync,
  fullscreen, and a built-in CRT scanline presentation preset. On Windows,
  the GPU preference can be set to system default, power-saving, or
  high-performance; the choice is stored as a per-application Windows graphics
  preference and takes effect after restarting the frontend.
- Uses SDL audio with WASAPI selected by default, a system-default audio
  backend option, and adjustable output gain.
- Detects supported 3DO BIOS filenames in the application's `bios` directory
  and uses that directory as the core system directory when enabled. Detection
  is optional; when no BIOS is found, the existing user system directory is used.
- Persists settings under the current user's SDL preferences directory and
  records diagnostics in `opera-frontend.log`.

The initial CRT preset is a scanline overlay, not a programmable shader chain;
loading arbitrary RetroArch `.glsl`, `.slang`, or `.preset` shaders is not yet
supported.
CHD files do not include copyrighted firmware. Any BIOS files must be legally
obtained by the user. When BIOS detection is enabled, place a supported BIOS
file in `bios` beside the application; the Core tab shows the detected file and
active system directory. Changing this option requires restarting the app.

## Build

Requirements: Windows 10 or later, CMake 3.24+, Git, and a C++20 compiler
(Visual Studio 2022 or newer). CMake downloads SDL3 and Dear ImGui at configure
time. On Windows, run these commands from a Visual Studio Developer PowerShell
so the compiler environment is available to Ninja.

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
the core from the launch window, then open a game image. The Core tab shows the
system and save directories. BIN/CUE sets should be opened through their `.cue` file.
Changing the video or audio backend requires restarting the application.

## Layout

- `src/LibretroCore.cpp`: libretro DLL lifecycle, callbacks, core options.
- `src/InputManager.cpp`: SDL gamepad discovery and two-player input routing.
- `src/AudioOutput.cpp`: SDL audio stream setup and PCM submission.
- `src/CoreDownloader.cpp`: HTTPS download, staged extraction, and installation.
- `src/Application.cpp`: SDL event loop, rendering, dialogs, and ImGui settings.
- `src/Settings.cpp`: validated INI settings persistence.
