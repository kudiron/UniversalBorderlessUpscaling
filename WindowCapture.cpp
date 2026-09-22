#include "WindowCapture.h"

namespace UBU {
namespace {

bool IsSystemClass(HWND hwnd) {
    wchar_t className[256] = {0};
    GetClassNameW(hwnd, className, 256);
    return wcscmp(className, L"Shell_TrayWnd") == 0 ||
           wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
           wcscmp(className, L"Progman") == 0 ||
           wcscmp(className, L"WorkerW") == 0 ||
           wcscmp(className, L"ForegroundStaging") == 0 ||
           wcscmp(className, L"UBU_OverlayClass") == 0 ||
           wcscmp(className, L"UBU_ControlWindowClass") == 0;
}

HWND RootWindow(HWND hwnd) {
    if (!hwnd) {
        return nullptr;
    }
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}

} // namespace

HWND WindowCapture::CaptureForegroundWindow(HWND excludeHwnd) {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        GUITHREADINFO gti{ sizeof(GUITHREADINFO) };
        if (GetGUIThreadInfo(0, &gti)) {
            fg = gti.hwndActive ? gti.hwndActive : gti.hwndFocus;
        }
    }

    fg = RootWindow(fg);
    if (!fg || fg == excludeHwnd) {
        return nullptr;
    }
    if (!IsValidTargetWindow(fg)) {
        return nullptr;
    }
    return fg;
}

bool WindowCapture::IsValidTargetWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return false;
    }
    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow()) {
        return false;
    }

    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid)) {
        return false;
    }
    if (pid == GetCurrentProcessId()) {
        return false;
    }

    if (IsSystemClass(hwnd)) {
        return false;
    }

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) {
        return false;
    }

    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) {
        return false;
    }
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width < 32 || height < 32) {
        return false;
    }

    return true;
}

} // namespace UBU
