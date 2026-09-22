# 🚀 Universal Borderless Upscaler (UBU) v2.1

[![Language](https://shields.io)](https://cppreference.com)
[![Platform](https://shields.io)](https://microsoft.com)
[![License](https://shields.io)](https://opensource.org)

**Universal Borderless Upscaler (UBU)** is a lightweight, zero-dependency Windows performance utility designed for low-end hardware (e.g., Intel UHD integrated graphics). It runs resource-heavy games at low windowed resolutions and upscales them onto a seamless, borderless fullscreen overlay using the GPU.

---

## 🔥 Key Features

- **Massive FPS Boost:** Play games at low internal resolutions rendered cleanly in fullscreen space.
- **Pure Win32 & DirectX 11 Optimization:** Built with native Windows APIs and DX11 pipelines with zero bloat.
- **Intel UHD Driver Stability:** Specialized fallback routines prevent memory leaks and display driver crashes.
- **Advanced Mouse Coordinate Scaling:** Combines raw mouse movement accumulation with precise OS cursor warping to eliminate dead zones.

---

## 🕹️ Hotkeys & Control Layer

- `Alt + Shift + Z` - **Toggle Upscaler On/Off**
- `Alt + Shift + 1` - **Mode 1: Nearest Neighbor** (~0 GPU cost)
- `Alt + Shift + 2` - **Mode 2: Bilinear Interpolation** (**Default Startup Mode**)
- `Alt + Shift + 3` - **Mode 3: Smart Sharpen**
- `Alt + Shift + V` - **Toggle VSync**

---

## 🏗️ Architectural Overview

UBU is modularly designed across independent layers:
- **Window Capture (`WindowCapture.h/.cpp`):** Captures focused window handles (`HWND`).
- **Style Modification (`StyleModifier.h/.cpp`):** Strips application titles and borders.
- **Scaling & Rendering (`UpscaleRenderer.h/.cpp`):** Processes DX11 custom upscaling shaders and overlays.
- **Cursor Management (`CursorManager.h/.cpp`):** System-wide hardware cursor hider.
- **Mouse Scaling (`MouseScaler.h/.cpp`):** Tracks cursor delta changes and warps OS inputs.

---

## ⚙️ How It Works
Run your game in Windowed Mode at a low resolution, click to focus, and press `Alt + Shift + Z`. The utility hooks the window, strips boundaries, deploys a borderless overlay (`WS_EX_NOACTIVATE`), and processes frame textures via DXGI Desktop Duplication or GDI `BitBlt`.

---

## 🛠️ Compilation & Building
Requires **C++17** or higher and DirectX 11. 

- **MinGW-w64 CLI:**
  ```bat
  g++ -std=c++17 -O2 -mwindows main.cpp WindowCapture.cpp StyleModifier.cpp UpscaleRenderer.cpp CursorManager.cpp MouseScaler.cpp -o UBU.exe -ld3d11 -ld3dcompiler -ldxgi -lgdi32 -luser32 -lpthread
  ```
- **CMake (Visual Studio / MinGW):** Available via standard `cmake -B build` configurations.

---

## 🩹 v2.1 Critical Patch Notes (Intel UHD Graphics Fix)
- Phased out problematic `PrintWindow` loops in favor of high-performance GDI `BitBlt` with memory recycling.
- Prioritized native DXGI Desktop Duplication and tightened acquisition timeouts to 4ms.
- Added automatic failure recovery to deactivate the upscaler after 10 consecutive capture failures.

---

## ⚠️ Known Limitations & License
- Requires games to start in windowed or borderless configurations.
- Licensed under the open-source **MIT License**.
