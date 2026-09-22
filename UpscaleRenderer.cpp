#include "UpscaleRenderer.h"

#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>
#include <wrl/client.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;

namespace UBU {
namespace {

const char* kShaderSource = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

Texture2D srcTex : register(t0);
SamplerState sampPoint : register(s0);
SamplerState sampLinear : register(s1);

float4 PSMain(VSOut i) : SV_TARGET
{
    float4 c = srcTex.Sample(sampPoint, i.uv);
    return float4(c.rgb, 1.0);
}

static const float kSharpenAmount = 0.9;
float4 PSMainSharpen(VSOut i) : SV_TARGET
{
    uint texWidth, texHeight;
    srcTex.GetDimensions(texWidth, texHeight);
    float2 texelSize = float2(1.0 / (float)texWidth, 1.0 / (float)texHeight);

    float4 center = srcTex.Sample(sampLinear, i.uv);
    float4 sum =
        srcTex.Sample(sampLinear, i.uv + float2(texelSize.x, 0)) +
        srcTex.Sample(sampLinear, i.uv - float2(texelSize.x, 0)) +
        srcTex.Sample(sampLinear, i.uv + float2(0, texelSize.y)) +
        srcTex.Sample(sampLinear, i.uv - float2(0, texelSize.y));
    float4 blurred = sum * 0.25;
    float4 outc = saturate(center + (center - blurred) * kSharpenAmount);
    return float4(outc.rgb, 1.0);
}

cbuffer CursorBuf : register(b0)
{
    float4 cursorNdc;
};

struct VSCursorOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSCursorOut VSCursor(uint vid : SV_VertexID)
{
    float2 c = float2((float)(vid & 1), (float)((vid >> 1) & 1));
    VSCursorOut o;
    o.uv = c;
    o.pos = float4(lerp(cursorNdc.x, cursorNdc.z, c.x), lerp(cursorNdc.y, cursorNdc.w, c.y), 0.0, 1.0);
    return o;
}

float4 PSCursor(VSCursorOut i) : SV_TARGET
{
    float4 c = srcTex.Sample(sampPoint, i.uv);
    if (c.a < 0.02) discard;
    return c;
}
)";

} // namespace

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(nullptr);
        return TRUE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

UpscaleRenderer::~UpscaleRenderer() {
    Shutdown();
}

bool UpscaleRenderer::Initialize(HMONITOR monitor, UpscaleMode mode) {
    Shutdown();

    if (!monitor) {
        return false;
    }

    m_targetMonitor = monitor;
    m_mode.store(mode);
    m_getSharedSurface = reinterpret_cast<DwmGetDxSharedSurfaceFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "DwmGetDxSharedSurface"));

    if (!CreateOverlayWindow(monitor)) {
        Shutdown();
        return false;
    }
    if (!CreateDeviceAndSwapChain()) {
        Shutdown();
        return false;
    }
    if (!CreateShaders()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void UpscaleRenderer::ShowOverlay(bool visible) {
    if (!m_overlayHwnd) {
        return;
    }

    m_overlayVisible = visible;
    ShowWindow(m_overlayHwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (visible) {
        SetWindowPos(
            m_overlayHwnd,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool UpscaleRenderer::CreateOverlayWindow(HMONITOR monitor) {
    if (!monitor) {
        return false;
    }

    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    m_outputWidth = mi.rcMonitor.right - mi.rcMonitor.left;
    m_outputHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (m_outputWidth <= 0 || m_outputHeight <= 0) {
        return false;
    }

    static const wchar_t* kClassName = L"UBU_OverlayClass";
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.hCursor = nullptr;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        s_registered = true;
    }

    m_overlayHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        kClassName,
        L"UBU Overlay",
        WS_POPUP,
        mi.rcMonitor.left,
        mi.rcMonitor.top,
        m_outputWidth,
        m_outputHeight,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!m_overlayHwnd) {
        return false;
    }

    if (!SetWindowDisplayAffinity(m_overlayHwnd, WDA_EXCLUDEFROMCAPTURE)) {
        DestroyWindow(m_overlayHwnd);
        m_overlayHwnd = nullptr;
        return false;
    }

    ShowWindow(m_overlayHwnd, SW_HIDE);
    m_overlayVisible = false;
    return true;
}

bool UpscaleRenderer::CreateDeviceAndSwapChain() {
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
        D3D11_SDK_VERSION,
        m_device.GetAddressOf(),
        &fl,
        m_context.GetAddressOf());

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            m_device.GetAddressOf(),
            &fl,
            m_context.GetAddressOf());
    }
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) {
        return false;
    }
    ComPtr<IDXGIDevice1> dxgiDevice1;
    if (SUCCEEDED(dxgiDevice.As(&dxgiDevice1))) {
        dxgiDevice1->SetMaximumFrameLatency(1);
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        return false;
    }
    ComPtr<IDXGIFactory2> factory2;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory2)))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(m_outputWidth);
    desc.Height = static_cast<UINT>(m_outputHeight);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swap1;
    hr = factory2->CreateSwapChainForHwnd(
        m_device.Get(),
        m_overlayHwnd,
        &desc,
        nullptr,
        nullptr,
        swap1.GetAddressOf());

    if (FAILED(hr)) {
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        desc.BufferCount = 1;
        hr = factory2->CreateSwapChainForHwnd(
            m_device.Get(),
            m_overlayHwnd,
            &desc,
            nullptr,
            nullptr,
            swap1.GetAddressOf());
        m_usingFlipModel = false;
    } else {
        m_usingFlipModel = true;
    }
    if (FAILED(hr)) {
        return false;
    }

    if (FAILED(swap1.As(&m_swapChain))) {
        return false;
    }

    factory2->MakeWindowAssociation(m_overlayHwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return false;
    }
    if (FAILED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf()))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rd, m_rasterizer.GetAddressOf()))) {
        return false;
    }

    return true;
}

bool UpscaleRenderer::CreateShaders() {
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> psPt;
    ComPtr<ID3DBlob> psSh;
    ComPtr<ID3DBlob> vsCur;
    ComPtr<ID3DBlob> psCur;
    ComPtr<ID3DBlob> err;

    auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out) -> bool {
        err.Reset();
        const HRESULT hr = D3DCompile(
            kShaderSource,
            strlen(kShaderSource),
            nullptr,
            nullptr,
            nullptr,
            entry,
            target,
            0,
            0,
            out.GetAddressOf(),
            err.GetAddressOf());
        return SUCCEEDED(hr) && out;
    };

    if (!compile("VSMain", "vs_4_0", vs) ||
        !compile("PSMain", "ps_4_0", psPt) ||
        !compile("PSMainSharpen", "ps_4_0", psSh) ||
        !compile("VSCursor", "vs_4_0", vsCur) ||
        !compile("PSCursor", "ps_4_0", psCur)) {
        return false;
    }

    if (FAILED(m_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf()))) {
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(psPt->GetBufferPointer(), psPt->GetBufferSize(), nullptr, m_pixelShaderPoint.GetAddressOf()))) {
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(psSh->GetBufferPointer(), psSh->GetBufferSize(), nullptr, m_pixelShaderSharpen.GetAddressOf()))) {
        return false;
    }
    if (FAILED(m_device->CreateVertexShader(vsCur->GetBufferPointer(), vsCur->GetBufferSize(), nullptr, m_vertexShaderCursor.GetAddressOf()))) {
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(psCur->GetBufferPointer(), psCur->GetBufferSize(), nullptr, m_pixelShaderCursor.GetAddressOf()))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(m_device->CreateSamplerState(&sd, m_samplerPoint.GetAddressOf()))) {
        return false;
    }

    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(m_device->CreateSamplerState(&sd, m_samplerLinear.GetAddressOf()))) {
        return false;
    }

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bd, m_blendAlpha.GetAddressOf()))) {
        return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.ByteWidth = 32;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&cbd, nullptr, m_cursorCB.GetAddressOf()))) {
        return false;
    }

    return true;
}

bool UpscaleRenderer::UpdateCursorTexture(HCURSOR cursor) {
    if (!cursor || !m_device) {
        return false;
    }
    if (cursor == m_loadedCursor && m_cursorSRV) {
        return true;
    }

    ICONINFO ii{};
    if (!GetIconInfo(cursor, &ii)) {
        return false;
    }
    m_cursorHotX = static_cast<int>(ii.xHotspot);
    m_cursorHotY = static_cast<int>(ii.yHotspot);

    BITMAP bmp{};
    HBITMAP shape = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    if (shape) {
        GetObjectW(shape, sizeof(bmp), &bmp);
    }
    const bool maskOnly = (ii.hbmColor == nullptr);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);

    int width = bmp.bmWidth > 0 ? bmp.bmWidth : GetSystemMetrics(SM_CXCURSOR);
    int height = bmp.bmHeight > 0 ? bmp.bmHeight : GetSystemMetrics(SM_CYCURSOR);
    if (maskOnly && height > 0) {
        height /= 2;
    }
    if (width <= 0) width = 32;
    if (height <= 0) height = 32;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (dc) DeleteDC(dc);
        return false;
    }

    HGDIOBJ old = SelectObject(dc, dib);
    HBRUSH mag = CreateSolidBrush(RGB(255, 0, 255));
    RECT fill{ 0, 0, width, height };
    FillRect(dc, &fill, mag);
    DeleteObject(mag);
    DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);

    auto* px = static_cast<uint8_t*>(bits);
    for (int i = 0; i < width * height; ++i) {
        uint8_t b = px[i * 4 + 0];
        uint8_t g = px[i * 4 + 1];
        uint8_t r = px[i * 4 + 2];
        if (r == 255 && g == 0 && b == 255) {
            px[i * 4 + 0] = 0;
            px[i * 4 + 1] = 0;
            px[i * 4 + 2] = 0;
            px[i * 4 + 3] = 0;
        } else if (px[i * 4 + 3] == 0) {
            px[i * 4 + 3] = 255;
        }
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bits;
    init.SysMemPitch = static_cast<UINT>(width * 4);

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_device->CreateTexture2D(&td, &init, tex.GetAddressOf());

    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteDC(dc);

    if (FAILED(hr)) {
        return false;
    }

    m_cursorSRV.Reset();
    if (FAILED(m_device->CreateShaderResourceView(tex.Get(), nullptr, m_cursorSRV.GetAddressOf()))) {
        return false;
    }

    m_cursorWidth = width;
    m_cursorHeight = height;
    m_loadedCursor = cursor;
    return true;
}

void UpscaleRenderer::SetCursorVisual(bool visible, HCURSOR cursor, int x, int y) {
    m_drawCursor = visible && cursor != nullptr;
    m_cursorX.store(x);
    m_cursorY.store(y);
    if (m_drawCursor) {
        UpdateCursorTexture(cursor);
    }
}

void UpscaleRenderer::RenderCursor() {
    if (!m_drawCursor || !m_cursorSRV || !m_vertexShaderCursor || !m_pixelShaderCursor || !m_cursorCB) {
        return;
    }
    if (m_outputWidth <= 0 || m_outputHeight <= 0) {
        return;
    }

    const float x = static_cast<float>(m_cursorX.load() - m_cursorHotX);
    const float y = static_cast<float>(m_cursorY.load() - m_cursorHotY);
    const float w = static_cast<float>(m_cursorWidth);
    const float h = static_cast<float>(m_cursorHeight);
    const float ow = static_cast<float>(m_outputWidth);
    const float oh = static_cast<float>(m_outputHeight);

    auto toNdcX = [&](float px) { return px / ow * 2.0f - 1.0f; };
    auto toNdcY = [&](float py) { return 1.0f - py / oh * 2.0f; };

    CursorCB cb{};
    cb.left = toNdcX(x);
    cb.top = toNdcY(y);
    cb.right = toNdcX(x + w);
    cb.bottom = toNdcY(y + h);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_cursorCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    m_context->Unmap(m_cursorCB.Get(), 0);

    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState(m_blendAlpha.Get(), blendFactor, 0xFFFFFFFF);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_context->VSSetShader(m_vertexShaderCursor.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShaderCursor.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_cursorCB.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, m_cursorSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_samplerPoint.GetAddressOf());
    m_context->Draw(4, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);
    m_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

void UpscaleRenderer::ReleaseGdiResources() {
    if (m_memDC && m_oldBitmap) {
        SelectObject(m_memDC, m_oldBitmap);
        m_oldBitmap = nullptr;
    }
    if (m_memBitmap) {
        DeleteObject(m_memBitmap);
        m_memBitmap = nullptr;
    }
    if (m_memDC) {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }
    m_dibBits = nullptr;
}

bool UpscaleRenderer::EnsureGpuTexture(int width, int height, DXGI_FORMAT format) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width == m_sourceWidth && height == m_sourceHeight && m_sourceTexture && m_sourceSRV && m_sourceFormat == format) {
        return true;
    }

    m_sourceWidth = width;
    m_sourceHeight = height;
    m_sourceFormat = format;
    m_sourceSRV.Reset();
    m_sourceTexture.Reset();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_sourceTexture.GetAddressOf()))) {
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        return false;
    }
    if (FAILED(m_device->CreateShaderResourceView(m_sourceTexture.Get(), nullptr, m_sourceSRV.GetAddressOf()))) {
        m_sourceTexture.Reset();
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        return false;
    }
    return true;
}

bool UpscaleRenderer::EnsureSourceTexture(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width == m_sourceWidth && height == m_sourceHeight && m_sourceTexture && m_memBitmap && m_dibBits) {
        return true;
    }

    m_sourceWidth = width;
    m_sourceHeight = height;
    m_sourceSRV.Reset();
    m_sourceTexture.Reset();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_sourceTexture.GetAddressOf()))) {
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        return false;
    }
    if (FAILED(m_device->CreateShaderResourceView(m_sourceTexture.Get(), nullptr, m_sourceSRV.GetAddressOf()))) {
        m_sourceTexture.Reset();
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        return false;
    }

    if (m_memDC && m_oldBitmap) {
        SelectObject(m_memDC, m_oldBitmap);
        m_oldBitmap = nullptr;
    }
    if (m_memBitmap) {
        DeleteObject(m_memBitmap);
        m_memBitmap = nullptr;
        m_dibBits = nullptr;
    }
    if (!m_memDC) {
        HDC screenDC = GetDC(nullptr);
        if (!screenDC) {
            m_sourceTexture.Reset();
            m_sourceSRV.Reset();
            m_sourceWidth = 0;
            m_sourceHeight = 0;
            return false;
        }
        m_memDC = CreateCompatibleDC(screenDC);
        ReleaseDC(nullptr, screenDC);
        if (!m_memDC) {
            m_sourceTexture.Reset();
            m_sourceSRV.Reset();
            m_sourceWidth = 0;
            m_sourceHeight = 0;
            return false;
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    m_memBitmap = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_memBitmap || !bits) {
        if (m_memBitmap) {
            DeleteObject(m_memBitmap);
            m_memBitmap = nullptr;
        }
        m_sourceTexture.Reset();
        m_sourceSRV.Reset();
        m_sourceWidth = 0;
        m_sourceHeight = 0;
        return false;
    }

    m_dibBits = static_cast<uint8_t*>(bits);
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_memBitmap));
    return true;
}

bool UpscaleRenderer::DibHasVisiblePixels() const {
    if (!m_dibBits || m_sourceWidth <= 0 || m_sourceHeight <= 0) {
        return false;
    }

    const int pitch = m_sourceWidth * 4;
    int visible = 0;
    for (int i = 0; i < 24; ++i) {
        const int x = (m_sourceWidth * (i + 1)) / 25;
        const int y = (m_sourceHeight * (((i * 7) % 24) + 1)) / 25;
        const uint8_t* p = m_dibBits + y * pitch + x * 4;
        if (p[0] | p[1] | p[2]) {
            ++visible;
        }
    }
    return visible > 0;
}

bool UpscaleRenderer::UploadDibToGpu() {
    if (!m_context || !m_sourceTexture || !m_dibBits) {
        return false;
    }
    m_context->UpdateSubresource(
        m_sourceTexture.Get(),
        0,
        nullptr,
        m_dibBits,
        static_cast<UINT>(m_sourceWidth * 4),
        0);
    return true;
}

bool UpscaleRenderer::InitDesktopDuplication() {
    if (m_deskDupl) {
        return true;
    }
    if (!m_device) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) {
        return false;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    if (FAILED(dxgiDevice->GetParent(IID_PPV_ARGS(&dxgiAdapter)))) {
        return false;
    }

    ComPtr<IDXGIOutput> dxgiOutput;
    UINT outputIndex = 0;
    bool foundOutput = false;

    while (dxgiAdapter->EnumOutputs(outputIndex, dxgiOutput.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND) {
        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(dxgiOutput->GetDesc(&desc))) {
            if (m_targetMonitor == nullptr || desc.Monitor == m_targetMonitor) {
                foundOutput = true;
                break;
            }
        }
        ++outputIndex;
    }

    if (!foundOutput) {
        dxgiOutput.Reset();
        if (FAILED(dxgiAdapter->EnumOutputs(0, dxgiOutput.GetAddressOf()))) {
            return false;
        }
    }

    ComPtr<IDXGIOutput1> dxgiOutput1;
    if (FAILED(dxgiOutput.As(&dxgiOutput1))) {
        return false;
    }

    const HRESULT hr = dxgiOutput1->DuplicateOutput(m_device.Get(), m_deskDupl.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        m_deskDupl.Reset();
        return false;
    }

    return true;
}

void UpscaleRenderer::ReleaseDesktopDuplication() {
    m_deskDupl.Reset();
}

bool UpscaleRenderer::CaptureFrameShared(HWND sourceHwnd) {
    if (!m_getSharedSurface || !m_device || !m_context) {
        return false;
    }

    HANDLE surface = nullptr;
    LUID adapterLuid{};
    ULONG fmt = 0;
    ULONG presentFlags = 0;
    ULONGLONG updateId = 0;
    const HRESULT hr = m_getSharedSurface(sourceHwnd, &surface, &adapterLuid, &fmt, &presentFlags, &updateId);
    if (FAILED(hr) || !surface) {
        return false;
    }

    ComPtr<ID3D11Texture2D> sharedTex;
    if (FAILED(m_device->OpenSharedResource(surface, IID_PPV_ARGS(&sharedTex)))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    sharedTex->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(sourceHwnd, &client)) {
        return false;
    }
    const int clientW = client.right - client.left;
    const int clientH = client.bottom - client.top;
    if (clientW <= 0 || clientH <= 0) {
        return false;
    }

    RECT windowRc{};
    GetWindowRect(sourceHwnd, &windowRc);
    POINT clientTL{ 0, 0 };
    ClientToScreen(sourceHwnd, &clientTL);
    int cropX = clientTL.x - windowRc.left;
    int cropY = clientTL.y - windowRc.top;
    if (cropX < 0) cropX = 0;
    if (cropY < 0) cropY = 0;

    int cropW = clientW;
    int cropH = clientH;
    if (cropX + cropW > static_cast<int>(desc.Width)) {
        cropW = static_cast<int>(desc.Width) - cropX;
    }
    if (cropY + cropH > static_cast<int>(desc.Height)) {
        cropH = static_cast<int>(desc.Height) - cropY;
    }
    if (cropW <= 0 || cropH <= 0) {
        return false;
    }

    DXGI_FORMAT format = desc.Format;
    if (format == DXGI_FORMAT_UNKNOWN) {
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    if (!EnsureGpuTexture(cropW, cropH, DXGI_FORMAT_B8G8R8A8_UNORM)) {
        return false;
    }

    D3D11_BOX box{};
    box.left = static_cast<UINT>(cropX);
    box.top = static_cast<UINT>(cropY);
    box.front = 0;
    box.right = static_cast<UINT>(cropX + cropW);
    box.bottom = static_cast<UINT>(cropY + cropH);
    box.back = 1;
    m_context->CopySubresourceRegion(m_sourceTexture.Get(), 0, 0, 0, 0, sharedTex.Get(), 0, &box);
    return true;
}

bool UpscaleRenderer::CaptureFrameDXGI(HWND sourceHwnd) {
    if (m_overlayVisible) {
        return false;
    }
    if (!m_deskDupl && !InitDesktopDuplication()) {
        return false;
    }

    RECT clientRc{};
    if (!GetClientRect(sourceHwnd, &clientRc)) {
        return false;
    }

    const int width = clientRc.right - clientRc.left;
    const int height = clientRc.bottom - clientRc.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    POINT pt{ clientRc.left, clientRc.top };
    if (!ClientToScreen(sourceHwnd, &pt)) {
        return false;
    }

    // Use shorter timeout to prevent freezing
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    HRESULT hr = m_deskDupl->AcquireNextFrame(4, &frameInfo, desktopResource.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (FAILED(hr)) {
        ReleaseDesktopDuplication();
        return false;
    }

    ComPtr<ID3D11Texture2D> capturedTexture;
    hr = desktopResource.As(&capturedTexture);
    bool copied = false;

    if (SUCCEEDED(hr) && capturedTexture && EnsureSourceTexture(width, height)) {
        D3D11_TEXTURE2D_DESC desc{};
        capturedTexture->GetDesc(&desc);

        if (m_targetMonitor) {
            MONITORINFO mi{ sizeof(MONITORINFO) };
            if (GetMonitorInfoW(m_targetMonitor, &mi)) {
                pt.x -= mi.rcMonitor.left;
                pt.y -= mi.rcMonitor.top;
            }
        }

        const int cropX = (std::max)(0, (std::min)(static_cast<int>(pt.x), static_cast<int>(desc.Width) - 1));
        const int cropY = (std::max)(0, (std::min)(static_cast<int>(pt.y), static_cast<int>(desc.Height) - 1));
        const int cropRight = (std::max)(cropX + 1, (std::min)(static_cast<int>(pt.x + width), static_cast<int>(desc.Width)));
        const int cropBottom = (std::max)(cropY + 1, (std::min)(static_cast<int>(pt.y + height), static_cast<int>(desc.Height)));

        if (static_cast<int>(cropRight - cropX) <= width && static_cast<int>(cropBottom - cropY) <= height) {
            D3D11_BOX srcBox{};
            srcBox.left = static_cast<UINT>(cropX);
            srcBox.top = static_cast<UINT>(cropY);
            srcBox.front = 0;
            srcBox.right = static_cast<UINT>(cropRight);
            srcBox.bottom = static_cast<UINT>(cropBottom);
            srcBox.back = 1;

            m_context->CopySubresourceRegion(
                m_sourceTexture.Get(),
                0, 0, 0, 0,
                capturedTexture.Get(),
                0,
                &srcBox);
            copied = true;
        }
    }

    m_deskDupl->ReleaseFrame();
    return copied;
}

bool UpscaleRenderer::CaptureFrameGDI(HWND sourceHwnd) {
    RECT rc{};
    if (!GetClientRect(sourceHwnd, &rc)) {
        return false;
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (!EnsureSourceTexture(width, height) || !m_memDC || !m_dibBits) {
        return false;
    }

    // Use BitBlt instead of PrintWindow for better stability on Intel UHD graphics
    HDC sourceDC = GetDC(sourceHwnd);
    if (!sourceDC) {
        return false;
    }

    BOOL captureResult = BitBlt(m_memDC, 0, 0, width, height, sourceDC, 0, 0, SRCCOPY);
    ReleaseDC(sourceHwnd, sourceDC);

    if (captureResult && DibHasVisiblePixels()) {
        return UploadDibToGpu();
    }

    return false;
}

bool UpscaleRenderer::CaptureFrame(HWND sourceHwnd) {
    if (!m_initialized || !m_device || !m_context) {
        return false;
    }
    if (!sourceHwnd || !IsWindow(sourceHwnd)) {
        return false;
    }

    // Try Desktop Duplication first (most stable for Intel UHD)
    if (!m_overlayVisible && CaptureFrameDXGI(sourceHwnd)) {
        return true;
    }

    // Fallback to GDI BitBlt (more stable than PrintWindow)
    if (CaptureFrameGDI(sourceHwnd)) {
        return true;
    }

    // Last resort: try shared surface (rarely works for games)
    if (CaptureFrameShared(sourceHwnd)) {
        return true;
    }

    return m_sourceSRV != nullptr;
}

void UpscaleRenderer::RenderFrame() {
    if (!m_context || !m_sourceSRV || !m_rtv || !m_swapChain) {
        return;
    }

    D3D11_VIEWPORT vp{
        0.0f,
        0.0f,
        static_cast<float>(m_outputWidth),
        static_cast<float>(m_outputHeight),
        0.0f,
        1.0f
    };
    m_context->RSSetViewports(1, &vp);
    m_context->RSSetState(m_rasterizer.Get());
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    ID3D11PixelShader* ps = m_pixelShaderPoint.Get();
    ID3D11SamplerState* samplers[] = { m_samplerPoint.Get(), m_samplerLinear.Get() };

    const UpscaleMode mode = m_mode.load();
    if (mode == UpscaleMode::Bilinear) {
        samplers[0] = m_samplerLinear.Get();
    } else if (mode == UpscaleMode::SmartSharpen) {
        ps = m_pixelShaderSharpen.Get();
        samplers[0] = m_samplerLinear.Get();
    }

    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(ps, nullptr, 0);
    m_context->PSSetShaderResources(0, 1, m_sourceSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 2, samplers);
    m_context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_context->PSSetShaderResources(0, 1, &nullSrv);

    RenderCursor();

    if (m_usingFlipModel) {
        m_context->OMSetRenderTargets(0, nullptr, nullptr);
    }

    const UINT syncInterval = m_vsyncEnabled.load() ? 1 : 0;
    m_swapChain->Present(syncInterval, 0);
}

void UpscaleRenderer::Shutdown() {
    ReleaseDesktopDuplication();
    ReleaseGdiResources();

    m_sourceSRV.Reset();
    m_sourceTexture.Reset();
    m_pixelShaderSharpen.Reset();
    m_pixelShaderPoint.Reset();
    m_vertexShader.Reset();
    m_vertexShaderCursor.Reset();
    m_pixelShaderCursor.Reset();
    m_cursorCB.Reset();
    m_cursorSRV.Reset();
    m_blendAlpha.Reset();
    m_samplerPoint.Reset();
    m_samplerLinear.Reset();
    m_rasterizer.Reset();
    m_rtv.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();

    m_sourceWidth = 0;
    m_sourceHeight = 0;
    m_sourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    m_initialized = false;
    m_overlayVisible = false;
    m_usingFlipModel = false;
    m_drawCursor = false;
    m_loadedCursor = nullptr;

    if (m_overlayHwnd) {
        DestroyWindow(m_overlayHwnd);
        m_overlayHwnd = nullptr;
    }
}

} // namespace UBU
