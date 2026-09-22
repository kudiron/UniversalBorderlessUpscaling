#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>

namespace UBU {

enum class UpscaleMode {
    NearestNeighbor,
    Bilinear,
    SmartSharpen
};

class UpscaleRenderer {
public:
    UpscaleRenderer() = default;
    ~UpscaleRenderer();

    UpscaleRenderer(const UpscaleRenderer&) = delete;
    UpscaleRenderer& operator=(const UpscaleRenderer&) = delete;

    bool Initialize(HMONITOR targetMonitor, UpscaleMode mode);
    void Shutdown();

    bool CaptureFrame(HWND sourceHwnd);
    void RenderFrame();

    void SetMode(UpscaleMode mode) { m_mode.store(mode); }
    UpscaleMode GetMode() const { return m_mode.load(); }

    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled.store(enabled); }
    bool IsVSyncEnabled() const { return m_vsyncEnabled.load(); }

    void SetCursorVisual(bool visible, HCURSOR cursor, int x, int y);

    HWND GetOverlayWindow() const { return m_overlayHwnd; }
    int GetOutputWidth() const { return m_outputWidth; }
    int GetOutputHeight() const { return m_outputHeight; }
    bool IsOverlayVisible() const { return m_overlayVisible; }

    void ShowOverlay(bool visible);

private:
    struct CursorCB {
        float left;
        float top;
        float right;
        float bottom;
        float pad[4];
    };

    using DwmGetDxSharedSurfaceFn = HRESULT (WINAPI*)(HWND, HANDLE*, LUID*, ULONG*, ULONG*, ULONGLONG*);

    bool CreateOverlayWindow(HMONITOR monitor);
    bool CreateDeviceAndSwapChain();
    bool CreateShaders();
    bool UpdateCursorTexture(HCURSOR cursor);
    void RenderCursor();
    bool EnsureGpuTexture(int width, int height, DXGI_FORMAT format);
    bool EnsureSourceTexture(int width, int height);
    void ReleaseGdiResources();
    bool UploadDibToGpu();
    bool DibHasVisiblePixels() const;

    bool CaptureFrameShared(HWND sourceHwnd);
    bool InitDesktopDuplication();
    void ReleaseDesktopDuplication();
    bool CaptureFrameDXGI(HWND sourceHwnd);
    bool CaptureFrameGDI(HWND sourceHwnd);

    HWND m_overlayHwnd = nullptr;
    HMONITOR m_targetMonitor = nullptr;
    std::atomic<UpscaleMode> m_mode{UpscaleMode::Bilinear};
    std::atomic<bool> m_vsyncEnabled{true};
    bool m_initialized = false;
    bool m_overlayVisible = false;
    bool m_usingFlipModel = false;
    bool m_drawCursor = false;
    HCURSOR m_loadedCursor = nullptr;
    DwmGetDxSharedSurfaceFn m_getSharedSurface = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendAlpha;

    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_deskDupl;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShaderPoint;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShaderSharpen;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShaderCursor;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShaderCursor;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cursorCB;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerPoint;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerLinear;

    std::atomic<int> m_cursorX{0};
    std::atomic<int> m_cursorY{0};
    int m_cursorHotX = 0;
    int m_cursorHotY = 0;
    int m_cursorWidth = 32;
    int m_cursorHeight = 32;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cursorSRV;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sourceTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sourceSRV;
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    DXGI_FORMAT m_sourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    HDC m_memDC = nullptr;
    HBITMAP m_memBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    uint8_t* m_dibBits = nullptr;

    int m_outputWidth = 0;
    int m_outputHeight = 0;
};

} // namespace UBU
