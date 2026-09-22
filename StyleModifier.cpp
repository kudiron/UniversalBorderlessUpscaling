#include "StyleModifier.h"

namespace UBU {

StyleModifier::SavedStyle StyleModifier::StripBorder(HWND hwnd) {
    SavedStyle saved;
    saved.hwnd = hwnd;
    saved.originalStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
    saved.originalExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    GetWindowRect(hwnd, &saved.windowRect);
    GetClientRect(hwnd, &saved.clientRect);
    saved.valid = true;

    const int clientW = saved.clientRect.right - saved.clientRect.left;
    const int clientH = saved.clientRect.bottom - saved.clientRect.top;

    if (clientW <= 0 || clientH <= 0) {
        saved.valid = false;
        return saved;
    }

    LONG_PTR newStyle = saved.originalStyle;
    newStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                  WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    newStyle |= WS_POPUP;
    SetWindowLongPtrW(hwnd, GWL_STYLE, newStyle);

    POINT tl{ 0, 0 };
    if (!ClientToScreen(hwnd, &tl)) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, saved.originalStyle);
        saved.valid = false;
        return saved;
    }

    SetWindowPos(
        hwnd,
        nullptr,
        tl.x,
        tl.y,
        clientW,
        clientH,
        SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

    return saved;
}

void StyleModifier::RestoreStyle(const SavedStyle& saved) {
    if (!saved.valid || !IsWindow(saved.hwnd)) {
        return;
    }
    SetWindowLongPtrW(saved.hwnd, GWL_STYLE, saved.originalStyle);
    SetWindowLongPtrW(saved.hwnd, GWL_EXSTYLE, saved.originalExStyle);
    SetWindowPos(
        saved.hwnd,
        nullptr,
        saved.windowRect.left,
        saved.windowRect.top,
        saved.windowRect.right - saved.windowRect.left,
        saved.windowRect.bottom - saved.windowRect.top,
        SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

} // namespace UBU
