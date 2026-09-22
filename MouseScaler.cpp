#include "MouseScaler.h"

namespace UBU {

MouseScaler::~MouseScaler() {
    Stop();
}

bool MouseScaler::RefreshSourceRect() {
    if (!m_sourceHwnd || !IsWindow(m_sourceHwnd)) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(m_sourceHwnd, &client)) {
        return false;
    }

    POINT topLeft{ client.left, client.top };
    POINT bottomRight{ client.right, client.bottom };
    if (!ClientToScreen(m_sourceHwnd, &topLeft) || !ClientToScreen(m_sourceHwnd, &bottomRight)) {
        return false;
    }

    if (bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) {
        return false;
    }

    m_sourceScreenRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    return true;
}

void MouseScaler::ApplySourceClip() {
    if (!RefreshSourceRect()) {
        return;
    }
    ClipCursor(&m_sourceScreenRect);
}

void MouseScaler::ReleaseClip() {
    ClipCursor(nullptr);
}

bool MouseScaler::Start(HWND /*messageWindow*/, HWND sourceHwnd, int outputWidth, int outputHeight) {
    Stop();

    if (!sourceHwnd || !IsWindow(sourceHwnd) || outputWidth <= 0 || outputHeight <= 0) {
        return false;
    }

    m_sourceHwnd = sourceHwnd;
    m_outputWidth = outputWidth;
    m_outputHeight = outputHeight;

    if (!RefreshSourceRect()) {
        m_sourceHwnd = nullptr;
        return false;
    }

    ApplySourceClip();
    if (!SetWindowPos(sourceHwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        ReleaseClip();
        m_sourceHwnd = nullptr;
        return false;
    }
    m_active.store(true);
    return true;
}

void MouseScaler::Stop() {
    m_active.store(false);
    ReleaseClip();
    m_sourceHwnd = nullptr;
    m_sourceScreenRect = {};
    m_outputWidth = 0;
    m_outputHeight = 0;
}

void MouseScaler::Refresh() {
    if (!m_active.load()) {
        return;
    }
    RefreshSourceRect();
}

bool MouseScaler::MapScreenToVirtual(POINT screen, int& virtualX, int& virtualY) const {
    const int srcW = m_sourceScreenRect.right - m_sourceScreenRect.left;
    const int srcH = m_sourceScreenRect.bottom - m_sourceScreenRect.top;
    if (srcW <= 0 || srcH <= 0 || m_outputWidth <= 0 || m_outputHeight <= 0) {
        return false;
    }

    float ratioX = static_cast<float>(screen.x - m_sourceScreenRect.left) / static_cast<float>(srcW);
    float ratioY = static_cast<float>(screen.y - m_sourceScreenRect.top) / static_cast<float>(srcH);
    if (ratioX < 0.0f) ratioX = 0.0f;
    if (ratioX > 1.0f) ratioX = 1.0f;
    if (ratioY < 0.0f) ratioY = 0.0f;
    if (ratioY > 1.0f) ratioY = 1.0f;

    virtualX = static_cast<int>(ratioX * static_cast<float>(m_outputWidth));
    virtualY = static_cast<int>(ratioY * static_cast<float>(m_outputHeight));
    return true;
}

} // namespace UBU
