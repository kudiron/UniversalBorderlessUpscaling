#pragma once
#include <windows.h>

namespace UBU {

// Module A: Window Capture
// Task: Finds the window that is currently in front (active/foreground) when
// the hotkey is pressed and returns its HWND identifier.
class WindowCapture {
public:
    // excludeHwnd: If there is a window that should be excluded (like our overlay
    // window), it is provided here (otherwise nullptr).
    static HWND CaptureForegroundWindow(HWND excludeHwnd);

    // Validates whether the captured window is actually a targetable "game/application"
    // window (filters out system windows like desktop, taskbar, etc.).
    static bool IsValidTargetWindow(HWND hwnd);
};

} // namespace UBU
