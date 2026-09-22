#include "CursorManager.h"

namespace UBU {
namespace {

using MagInitializeFn = BOOL (WINAPI*)();
using MagUninitializeFn = BOOL (WINAPI*)();
using MagShowSystemCursorFn = BOOL (WINAPI*)(BOOL);

HMODULE g_magModule = nullptr;
MagInitializeFn g_MagInitialize = nullptr;
MagUninitializeFn g_MagUninitialize = nullptr;
MagShowSystemCursorFn g_MagShowSystemCursor = nullptr;
bool g_magReady = false;

} // namespace

bool CursorManager::Initialize() {
    if (g_magReady) {
        return true;
    }

    g_magModule = LoadLibraryW(L"Magnification.dll");
    if (!g_magModule) {
        return false;
    }

    g_MagInitialize = reinterpret_cast<MagInitializeFn>(GetProcAddress(g_magModule, "MagInitialize"));
    g_MagUninitialize = reinterpret_cast<MagUninitializeFn>(GetProcAddress(g_magModule, "MagUninitialize"));
    g_MagShowSystemCursor = reinterpret_cast<MagShowSystemCursorFn>(GetProcAddress(g_magModule, "MagShowSystemCursor"));

    if (!g_MagInitialize || !g_MagUninitialize || !g_MagShowSystemCursor) {
        FreeLibrary(g_magModule);
        g_magModule = nullptr;
        g_MagInitialize = nullptr;
        g_MagUninitialize = nullptr;
        g_MagShowSystemCursor = nullptr;
        return false;
    }

    g_magReady = g_MagInitialize() != FALSE;
    if (!g_magReady) {
        FreeLibrary(g_magModule);
        g_magModule = nullptr;
        g_MagInitialize = nullptr;
        g_MagUninitialize = nullptr;
        g_MagShowSystemCursor = nullptr;
    }
    return g_magReady;
}

void CursorManager::Shutdown() {
    RestoreHardwareCursor();
    if (g_magReady && g_MagUninitialize) {
        g_MagUninitialize();
    }
    g_magReady = false;
    g_MagInitialize = nullptr;
    g_MagUninitialize = nullptr;
    g_MagShowSystemCursor = nullptr;
    if (g_magModule) {
        FreeLibrary(g_magModule);
        g_magModule = nullptr;
    }
}

void CursorManager::HideHardwareCursor() {
    if (!g_magReady) {
        if (!Initialize()) {
            return;
        }
    }
    if (g_magReady && g_MagShowSystemCursor) {
        g_MagShowSystemCursor(FALSE);
    }
}

void CursorManager::RestoreHardwareCursor() {
    if (g_magReady && g_MagShowSystemCursor) {
        g_MagShowSystemCursor(TRUE);
    }
}

CursorManager::Info CursorManager::QueryGameCursor() {
    Info info{};
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) {
        return info;
    }

    info.screenPos = ci.ptScreenPos;
    info.cursor = ci.hCursor;
    info.visible = (ci.flags & CURSOR_SHOWING) != 0 && ci.hCursor != nullptr;

    RECT clip{};
    if (GetClipCursor(&clip)) {
        const int clipW = clip.right - clip.left;
        const int clipH = clip.bottom - clip.top;
        if (clipW > 0 && clipW <= 4 && clipH > 0 && clipH <= 4) {
            info.visible = false;
        }
    }

    return info;
}

} // namespace UBU
