#include <windows.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>

#include "UpscaleRenderer.h"
#include "MouseScaler.h"
#include "StyleModifier.h"
#include "CursorManager.h"
#include "WindowCapture.h"

#define HOTKEY_TOGGLE_UPSCALE 1001
#define HOTKEY_MODE_NEAREST   1002
#define HOTKEY_MODE_BILINEAR  1003
#define HOTKEY_MODE_SHARPEN   1004
#define HOTKEY_TOGGLE_VSYNC   1005

namespace {
    std::atomic<bool> g_running{ true };
    std::atomic<bool> g_upscaleActive{ false };
    std::mutex g_stateMutex;
    HWND g_targetHwnd = nullptr;
    UBU::StyleModifier::SavedStyle g_savedStyle{};

    UBU::UpscaleRenderer g_renderer;
    UBU::MouseScaler g_mouseScaler;

    // Release cursor clipping
    void ReleaseCursorClip() {
        ClipCursor(nullptr);
    }

    // Force focus to the target window
    void FocusTarget(HWND hwnd) {
        if (!hwnd || !IsWindow(hwnd)) {
            return;
        }
        ShowWindow(hwnd, SW_SHOW);
        HWND fore = GetForegroundWindow();
        DWORD foreTid = 0;
        if (fore) {
            foreTid = GetWindowThreadProcessId(fore, nullptr);
        }
        const DWORD thisTid = GetCurrentThreadId();
        const DWORD targetTid = GetWindowThreadProcessId(hwnd, nullptr);
        if (foreTid && foreTid != thisTid) {
            AttachThreadInput(thisTid, foreTid, TRUE);
        }
        if (targetTid && targetTid != thisTid) {
            AttachThreadInput(thisTid, targetTid, TRUE);
        }
        SetForegroundWindow(hwnd);
        if (foreTid && foreTid != thisTid) {
            AttachThreadInput(thisTid, foreTid, FALSE);
        }
        if (targetTid && targetTid != thisTid) {
            AttachThreadInput(thisTid, targetTid, FALSE);
        }
    }

    // Deactivate upscaling and restore system state
    void DeactivateUpscale() {
        g_upscaleActive.store(false);

        g_mouseScaler.Stop();
        ReleaseCursorClip();
        UBU::CursorManager::RestoreHardwareCursor();
        g_renderer.ShowOverlay(false);

        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_targetHwnd && IsWindow(g_targetHwnd) && g_savedStyle.valid) {
            UBU::StyleModifier::RestoreStyle(g_savedStyle);
        }
        g_targetHwnd = nullptr;
        g_savedStyle = {};
    }

    // Main rendering thread function
    void RenderThreadFunc() {
        int consecutiveFailures = 0;
        const int maxFailures = 10;

        while (g_running.load(std::memory_order_relaxed)) {
            if (!g_upscaleActive.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                consecutiveFailures = 0;
                continue;
            }

            HWND currentTarget = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                currentTarget = g_targetHwnd;
            }

            if (!currentTarget || !IsWindow(currentTarget) || IsIconic(currentTarget)) {
                DeactivateUpscale();
                consecutiveFailures = 0;
                continue;
            }

            if (g_renderer.CaptureFrame(currentTarget)) {
                g_mouseScaler.Refresh();
                const auto cursor = UBU::CursorManager::QueryGameCursor();
                int vx = 0;
                int vy = 0;
                if (cursor.visible) {
                    g_mouseScaler.MapScreenToVirtual(cursor.screenPos, vx, vy);
                }
                g_renderer.SetCursorVisual(cursor.visible, cursor.cursor, vx, vy);
                g_renderer.RenderFrame();
                if (!g_renderer.IsOverlayVisible()) {
                    g_renderer.ShowOverlay(true);
                    FocusTarget(currentTarget);
                }
                consecutiveFailures = 0;
            } else {
                consecutiveFailures++;
                if (consecutiveFailures >= maxFailures) {
                    DeactivateUpscale();
                    consecutiveFailures = 0;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        }

        ReleaseCursorClip();
    }
}

LRESULT CALLBACK ControlWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RegisterHotKey(hwnd, HOTKEY_TOGGLE_UPSCALE, MOD_ALT | MOD_SHIFT, 'Z');
        RegisterHotKey(hwnd, HOTKEY_MODE_NEAREST,   MOD_ALT | MOD_SHIFT, '1');
        RegisterHotKey(hwnd, HOTKEY_MODE_BILINEAR,  MOD_ALT | MOD_SHIFT, '2');
        RegisterHotKey(hwnd, HOTKEY_MODE_SHARPEN,   MOD_ALT | MOD_SHIFT, '3');
        RegisterHotKey(hwnd, HOTKEY_TOGGLE_VSYNC,   MOD_ALT | MOD_SHIFT, 'V');
        break;
    }

    case WM_HOTKEY: {
        switch (wParam) {
        case HOTKEY_TOGGLE_UPSCALE: {
            const bool nextState = !g_upscaleActive.load();
            if (nextState) {
                HWND overlayHwnd = g_renderer.GetOverlayWindow();
                HWND fgWnd = UBU::WindowCapture::CaptureForegroundWindow(overlayHwnd);
                if (!fgWnd || !UBU::WindowCapture::IsValidTargetWindow(fgWnd)) {
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(g_stateMutex);
                    g_targetHwnd = fgWnd;
                    g_savedStyle = UBU::StyleModifier::StripBorder(g_targetHwnd);
                }

                FocusTarget(fgWnd);
                UBU::CursorManager::HideHardwareCursor();

                const int outW = g_renderer.GetOutputWidth();
                const int outH = g_renderer.GetOutputHeight();
                if (!g_mouseScaler.Start(hwnd, fgWnd, outW, outH)) {
                    DeactivateUpscale();
                    break;
                }

                g_upscaleActive.store(true);
            } else {
                DeactivateUpscale();
            }
            break;
        }
        case HOTKEY_MODE_NEAREST:
            g_renderer.SetMode(UBU::UpscaleMode::NearestNeighbor);
            break;
        case HOTKEY_MODE_BILINEAR:
            g_renderer.SetMode(UBU::UpscaleMode::Bilinear);
            break;
        case HOTKEY_MODE_SHARPEN:
            g_renderer.SetMode(UBU::UpscaleMode::SmartSharpen);
            break;
        case HOTKEY_TOGGLE_VSYNC:
            g_renderer.SetVSyncEnabled(!g_renderer.IsVSyncEnabled());
            break;
        default:
            break;
        }
        break;
    }

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE_UPSCALE);
        UnregisterHotKey(hwnd, HOTKEY_MODE_NEAREST);
        UnregisterHotKey(hwnd, HOTKEY_MODE_BILINEAR);
        UnregisterHotKey(hwnd, HOTKEY_MODE_SHARPEN);
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE_VSYNC);

        DeactivateUpscale();
        g_running.store(false);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HMONITOR primaryMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (!primaryMonitor) {
        return -1;
    }

    if (!UBU::CursorManager::Initialize()) {
        return -1;
    }

    if (!g_renderer.Initialize(primaryMonitor, UBU::UpscaleMode::Bilinear)) {
        UBU::CursorManager::Shutdown();
        return -1;
    }

    const wchar_t* kControlClassName = L"UBU_ControlWindowClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ControlWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kControlClassName;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        g_renderer.Shutdown();
        UBU::CursorManager::Shutdown();
        return -1;
    }

    HWND controlHwnd = CreateWindowExW(
        0, kControlClassName, L"UBU Control",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr
    );

    if (!controlHwnd) {
        g_renderer.Shutdown();
        UBU::CursorManager::Shutdown();
        return -1;
    }

    std::thread renderThread(RenderThreadFunc);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running.store(false);
    DeactivateUpscale();

    if (renderThread.joinable()) {
        renderThread.join();
    }

    ReleaseCursorClip();
    g_renderer.Shutdown();
    UBU::CursorManager::Shutdown();
    return 0;
}
