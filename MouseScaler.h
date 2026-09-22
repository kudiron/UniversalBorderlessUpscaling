#pragma once
#include <windows.h>
#include <atomic>

namespace UBU {

// Module F: Mouse Scaling
// Task: Tracks raw (relative) mouse movement in fullscreen space, warps the real OS cursor
// proportionally to the small source window
class MouseScaler {
public:
    MouseScaler() = default;
    ~MouseScaler();

    MouseScaler(const MouseScaler&) = delete;
    MouseScaler& operator=(const MouseScaler&) = delete;

    bool Start(HWND messageWindow, HWND sourceHwnd, int outputWidth, int outputHeight);
    void Stop();
    void Refresh();
    bool MapScreenToVirtual(POINT screen, int& virtualX, int& virtualY) const;
    RECT GetSourceScreenRect() const { return m_sourceScreenRect; }
    bool IsActive() const { return m_active.load(); }

private:
    bool RefreshSourceRect();
    void ApplySourceClip();
    void ReleaseClip();

    HWND m_sourceHwnd = nullptr;
    RECT m_sourceScreenRect{};
    int m_outputWidth = 0;
    int m_outputHeight = 0;
    std::atomic<bool> m_active{false};
};

} // namespace UBU
