#pragma once
#include <windows.h>

namespace UBU {

// Module E: Cursor Management
// Task: Hides the system cursor when active across the entire screen, restores when closed
class CursorManager {
public:
    static bool Initialize();
    static void Shutdown();

    static void HideHardwareCursor();
    static void RestoreHardwareCursor();

    struct Info {
        bool visible = false;
        HCURSOR cursor = nullptr;
        POINT screenPos{};
    };
    static Info QueryGameCursor();
};

} // namespace UBU
