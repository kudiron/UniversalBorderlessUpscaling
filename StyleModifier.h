#pragma once
#include <windows.h>

namespace UBU {

// Module B: Style Modification
// Task: Removes title bar and borders from the target window, restores them when closed
class StyleModifier {
public:
    struct SavedStyle {
        HWND hwnd = nullptr;
        LONG_PTR originalStyle = 0;
        LONG_PTR originalExStyle = 0;
        RECT windowRect{};
        RECT clientRect{};
        bool valid = false;
    };

    static SavedStyle StripBorder(HWND hwnd);
    static void RestoreStyle(const SavedStyle& saved);
};

} // namespace UBU
