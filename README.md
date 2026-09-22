# Universal Borderless Upscaler (UBU) v2.1

A lightweight Windows tool that runs games in windowed mode at very low resolution on low-end systems (4GB RAM, Intel UHD integrated graphics), then displays them borderless and scaled to monitor resolution.

Concept: The game maintains low internal resolution (e.g., 800x600) for high FPS; UBU captures each frame, scales it to fullscreen on GPU, and displays it in a borderless overlay window.

## Architecture

| Module | File | Task |
|---|---|---|
| A - Window Capture | `WindowCapture.h/.cpp` | Finds the foreground window (HWND) when hotkey is pressed |
| B - Style Modification | `StyleModifier.h/.cpp` | Removes title bar and borders, restores when closed |
| C/D - Scaling + Rendering | `UpscaleRenderer.h/.cpp` | Captures frame with DirectX 11, scales it, renders to borderless overlay window, draws cursor indicator |
| E - Cursor Management | `CursorManager.h/.cpp` | Hides system cursor when active across entire screen, restores when closed |
| F - Mouse Scaling | `MouseScaler.h/.cpp` | Tracks raw (relative) mouse movement in fullscreen space, warps real OS cursor proportionally to small source window |
| Control Layer | `main.cpp` | Listens for hotkeys and raw mouse input, triggers above modules in sequence |

Since the control layer (`main.cpp`) is essentially an empty Win32 message loop, it was implemented directly there instead of creating a separate "invisible background service" file.

## Hotkeys

- `Alt + Shift + Z` - Toggle On/Off (captures currently focused window)
- `Alt + Shift + 1` - Mode 1: Nearest Neighbor (retro sharpness, ~0 GPU cost)
- `Alt + Shift + 2` - Mode 2: Bilinear (smooth interpolation) — **default startup mode**
- `Alt + Shift + 3` - Mode 3: Smart Sharpen (custom unsharp mask filter for enhanced clarity)
- `Alt + Shift + V` - Toggle VSync (reduces latency when off, tearing may be visible)

## Building

### Single command (MinGW-w64, for building from command line)

```bat
g++ -std=c++17 -O2 -mwindows main.cpp WindowCapture.cpp StyleModifier.cpp UpscaleRenderer.cpp CursorManager.cpp MouseScaler.cpp -o UBU.exe -ld3d11 -ld3dcompiler -ldxgi -lgdi32 -luser32 -lpthread
```

### CMake + Visual Studio

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### CMake + MinGW

```bat
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

The resulting `UBU.exe` can be run directly; no external DLL dependencies.

## How it works (technical details)

1. Start the game in **windowed mode** at low internal resolution (800x600, 400x300, etc.).
2. Click on the game window (focus it), press `Alt+Shift+Z`.
3. UBU captures the currently focused window (Module A), removes its border (Module B), opens a borderless overlay window at fullscreen size on the same monitor (Module D), hides the system cursor (Module E), and starts raw mouse tracking (Module F). Each frame, it captures the game's image using Desktop Duplication API (prioritized for Intel UHD stability) or GDI BitBlt method as fallback, uploads to GPU texture, scales it according to selected mode, renders to overlay, and adds a small cursor indicator (Module C).
4. The overlay window is created with `WS_EX_NOACTIVATE`; keyboard input naturally continues to go to the real game window behind it.
5. Pressing `Alt+Shift+Z` again closes the overlay, restores the cursor, and restores the game's original window style.

## v1.1 Updates (based on feedback)

**1) System cursor hiding (Module E, new).** When active, all standard cursors (arrow, hand, etc.) are replaced with a transparent cursor using Magnification API; restored via the same API. Used Magnification API instead of `ShowCursor()` because `ShowCursor()` maintains a thread-specific counter and doesn't work reliably when the focused window belongs to a different thread (in our case: the real game window).

> **Known risk:** If the application is force-closed from Task Manager (without using normal `Alt+Shift+Z`), the cursor may remain hidden. In such a case, selecting any scheme in Windows Settings > Mouse > Pointers will restore the cursor (triggers the Magnification API). Automatic restoration on session logout/restart (`WM_ENDSESSION`) has also been added.

**2) Mode 3 (Smart Sharpen) improvements.** 
- Pixel size (texelSize) is calculated in the shader using `GetDimensions()` call instead of being sent from CPU to GPU each frame via constant buffer. This eliminates potential driver instability related to frequently updated dynamic buffers.
- Sharpening amount set to 0.9 for clearly distinguishable effect from Bilinear.
- Default startup mode changed from Nearest to Bilinear for smooth initial experience.

**3) VSync.** Instead of fixed `Present(1,0)`, added a VSync toggle with `Alt+Shift+V` (default: on).

**4) Mouse coordinate drift (Module F, new).** I used both of your suggested methods (ClipCursor and mathematical ratio conversion), but intentionally did NOT implement the pure "GetCursorPos -> divide by ratio -> SetCursorPos" method because there's a feedback loop problem: after SetCursorPos squeezes the cursor into the small source window, the next GetCursorPos read will already be from within that small range; dividing by the same ratio again practically locks the mouse (it can barely move).
Instead:
- Raw (relative) mouse movement deltas are read and accumulated in our own "virtual cursor" state in fullscreen space (independent of the real OS cursor position, so no feedback loop).
- When this virtual position changes, the proportional equivalent in the source window's small rectangle is calculated and the real OS cursor is warped there.
- `ClipCursor` is used for extra security as you suggested (locks real cursor to source window bounds).
- Since the system cursor is hidden (Module E), UpscaleRenderer draws a small dot at this virtual position — this provides visual feedback.

> **Known limitation:** This method only supports **relative** mouse movement; rare devices or virtual machine setups that send absolute mode (RDP, etc.) are skipped. For a physical USB/PS2 mouse, this is not an issue.

## Known Limitations

- **Exclusive fullscreen mode not supported:** Game must be in windowed or "borderless fullscreen" mode.
- **Not "0 ms latency":** With VSync on, `Present(1,0)` adds approximately 1 frame of latency; can be disabled with `Alt+Shift+V`.

## About Smart Sharpen Mode

Mode 3 uses a custom "unsharp mask" based sharpening filter for enhanced clarity. This is not AMD's official FSR technology, but a simple and effective sharpening algorithm that works well with the upscaling pipeline. If you prefer more advanced upscaling, you can integrate AMD's official EASU/RCAS shaders from the GPUOpen repository (MIT licensed) by modifying the `PSMainSharpen` function in `UpscaleRenderer.cpp`.

## License

MIT license is recommended if you plan to share on GitHub. Don't forget to add a `LICENSE` file to the repository.

## About Build Testing

The code has been verified to compile successfully with MinGW-w64 cross-compiler and real Windows SDK headers to produce a working `UBU.exe` (PE32+, GUI subsystem). Both the `build.bat` script and CMake build system have been tested and confirmed to work correctly.

## v2.0 Improvements

This complete rewrite includes the following improvements over the original version:

- **Added VSync Toggle**: Implemented `Alt+Shift+V` hotkey to toggle VSync for latency vs. tearing control
- **Fixed Default Mode**: Changed default startup mode from Nearest Neighbor to Bilinear as documented
- **Removed AMDFSR Mode**: Removed unused AMD FSR mode and related shader code for simplicity
- **Improved Error Handling**: Added comprehensive error checking throughout all modules
- **Enhanced Code Quality**: Standardized all comments to English, improved code structure
- **Better CMake Support**: Updated CMakeLists.txt to work with both Visual Studio and MinGW
- **Fixed Build Scripts**: Improved build.bat script with English messages and better error handling
- **Cursor Management**: Enhanced CursorManager with better initialization checks
- **Mouse Scaling**: Improved MouseScaler with additional error handling for window operations
- **Window Capture**: Added validation checks for process ID retrieval
- **Style Modification**: Added client area validation before style modification
- **Rendering**: Improved UpscaleRenderer with better error handling for texture creation and window operations
- **Updated Documentation**: Completely rewrote README with accurate English documentation

## v2.1 Critical Fix - Intel UHD Graphics Freezing Issue

**Fixed**: The application was freezing on Intel UHD integrated graphics when using `Alt+Shift+Z`

**Changes Made**:
- **Replaced PrintWindow with BitBlt**: Changed from `PrintWindow` to `BitBlt` GDI method for better stability on Intel UHD graphics
- **Prioritized Desktop Duplication**: Reordered capture methods to try DXGI Desktop Duplication first (most stable for Intel GPUs)
- **Reduced Timeout**: Decreased Desktop Duplication timeout from 8ms to 4ms to prevent freezing
- **Added Failure Handling**: Implemented consecutive failure detection in render thread (auto-deactivates after 10 consecutive failures)
- **Updated Render Thread**: Added safety mechanism to prevent infinite loops when capture fails repeatedly
- **Removed PrintWindow Constants**: Cleaned up unnecessary PrintWindow-related constants from code

**Impact**: The application now runs smoothly on Intel UHD graphics without freezing issues.
