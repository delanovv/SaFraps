#include "SurfacePool.h"

SurfacePool::SurfacePool(Logger& logger) : m_logger(logger) {}

bool SurfacePool::initialize(IDirect3DDevice9* device, UINT width, UINT height,
                              D3DFORMAT format, int count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();

    for (int i = 0; i < count; ++i) {
        ComPtr<IDirect3DSurface9> surface;
        HRESULT hr = device->CreateOffscreenPlainSurface(
            width, height, format, D3DPOOL_SYSTEMMEM, &surface, nullptr);

        if (FAILED(hr)) {
            m_logger.log("SurfacePool: failed to create surface " + std::to_string(i),
                         LogLevel::ERR);
            return false;
        }
        m_pool.push_back(surface);
    }

    m_cachedWidth  = width;
    m_cachedHeight = height;
    m_cachedFormat = format;
    return true;
}

void SurfacePool::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.clear();
}

ComPtr<IDirect3DSurface9> SurfacePool::acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pool.empty())
        return nullptr;
    auto surface = m_pool.back();
    m_pool.pop_back();
    return surface;
}

void SurfacePool::release(ComPtr<IDirect3DSurface9> surface) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.push_back(std::move(surface));
}

bool SurfacePool::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pool.empty();
}

bool SurfacePool::needsReinit(UINT width, UINT height, D3DFORMAT format) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pool.empty()
        || width  != m_cachedWidth
        || height != m_cachedHeight
        || format != m_cachedFormat;
}
