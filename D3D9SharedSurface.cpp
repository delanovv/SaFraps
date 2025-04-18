#include "D3D9SharedSurface.h"
#include <iostream>

D3D9SharedSurface::D3D9SharedSurface() {}

D3D9SharedSurface::~D3D9SharedSurface() {
    if (m_sharedSurface) m_sharedSurface->Release();
    if (m_sharedTexture) m_sharedTexture->Release();
    if (m_intermediateSurface) m_intermediateSurface->Release();
    if (m_sharedHandle) CloseHandle(m_sharedHandle);
    m_sharedSurface = nullptr;
    m_sharedTexture = nullptr;
    m_intermediateSurface = nullptr;
    m_sharedHandle = nullptr;
}

bool D3D9SharedSurface::initialize(IDirect3DDevice9* gameDevice, int width, int height) {
    if (!gameDevice) {
        std::cerr << "[D3D9Shared] Invalid game device\n";
        return false;
    }

    if (width <= 0 || height <= 0) {
        std::cerr << "[D3D9Shared] Invalid dimensions: " << width << "x" << height << "\n";
        return false;
    }

    m_width = width;
    m_height = height;

    // Получаем формат backbuffer
    IDirect3DSurface9* backBuffer = nullptr;
    HRESULT hr = gameDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] Failed to get backbuffer: " << (hr) << "\n";
        return false;
    }
    D3DSURFACE_DESC desc;
    backBuffer->GetDesc(&desc);
    D3DFORMAT format = desc.Format;
    bool useIntermediate = (desc.MultiSampleType != D3DMULTISAMPLE_NONE);
    backBuffer->Release();

    // Проверяем поддержку формата
    IDirect3D9* d3d9 = nullptr;
    hr = gameDevice->GetDirect3D(&d3d9);
    if (FAILED(hr) || !d3d9) {
        std::cerr << "[D3D9Shared] Failed to get IDirect3D9: " << (hr) << "\n";
        return false;
    }

    hr = d3d9->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, format, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, format);
    d3d9->Release();
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] Format " << format << " not supported for render target: " << (hr) << "\n";
        // Пробуем запасной формат
        format = D3DFMT_A8R8G8B8;
    }

    // Создаем общую текстуру
    hr = gameDevice->CreateTexture(
        m_width, m_height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,
        &m_sharedTexture, &m_sharedHandle);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] Failed to create shared texture. Width: " << m_width
            << ", Height: " << m_height << ", Format: " << format
            << ", HRESULT: " << (hr) << "\n";
        return false;
    }

    hr = m_sharedTexture->GetSurfaceLevel(0, &m_sharedSurface);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] Failed to get surface: " << (hr) << "\n";
        m_sharedTexture->Release();
        m_sharedTexture = nullptr;
        return false;
    }

    // Создаем промежуточную поверхность для мультисэмплинга
    if (useIntermediate) {
        hr = gameDevice->CreateRenderTarget(
            m_width, m_height, format, D3DMULTISAMPLE_NONE, 0, FALSE,
            &m_intermediateSurface, nullptr);
        if (FAILED(hr)) {
            std::cerr << "[D3D9Shared] Failed to create intermediate surface: " << (hr) << "\n";
            m_sharedSurface->Release();
            m_sharedTexture->Release();
            m_sharedSurface = nullptr;
            m_sharedTexture = nullptr;
            return false;
        }
    }

    std::cout << "[D3D9Shared] Initialized. HANDLE = " << m_sharedHandle
        << ", Width = " << m_width << ", Height = " << m_height << ", Format = " << format << "\n";
    return true;
}


class SurfaceRAII {
    IDirect3DSurface9* ptr = nullptr;
public:
    SurfaceRAII() = default;
    explicit SurfaceRAII(IDirect3DSurface9* p) : ptr(p) {}
    ~SurfaceRAII() { if (ptr) ptr->Release(); }
    IDirect3DSurface9* get() const { return ptr; }
    SurfaceRAII(const SurfaceRAII&) = delete;
    SurfaceRAII& operator=(const SurfaceRAII&) = delete;
};

bool D3D9SharedSurface::copyFromBackbuffer(IDirect3DDevice9* gameDevice, const RECT* pSrcRect) {
    if (!m_sharedSurface || !gameDevice) {
        std::cerr << "[D3D9Shared] Invalid state\n";
        return false;
    }

    IDirect3DSurface9* backBuffer = nullptr;
    HRESULT hr = gameDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] GetBackBuffer failed: " << (hr) << "\n";
        return false;
    }
    SurfaceRAII backBufferRAII(backBuffer);

    RECT destRect = { 0, 0, m_width, m_height };

    // Копируем через промежуточную поверхность, если есть мультисэмплинг
    IDirect3DSurface9* targetSurface = m_sharedSurface;
    if (m_intermediateSurface) {
        hr = gameDevice->StretchRect(backBuffer, pSrcRect, m_intermediateSurface, pSrcRect ? &destRect : nullptr, D3DTEXF_NONE);
        if (FAILED(hr)) {
            std::cerr << "[D3D9Shared] StretchRect to intermediate failed: " << (hr) << "\n";
            return false;
        }
        targetSurface = m_intermediateSurface;
    }

    // Копируем в общую текстуру
    hr = gameDevice->StretchRect(targetSurface, pSrcRect, m_sharedSurface, pSrcRect ? &destRect : nullptr, D3DTEXF_NONE);
    if (FAILED(hr)) {
        std::cerr << "[D3D9Shared] StretchRect failed: " << (hr) << "\n";
        return false;
    }

    return true;
}