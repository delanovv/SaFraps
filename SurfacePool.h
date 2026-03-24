#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>
#include "Logger.h"

using Microsoft::WRL::ComPtr;

struct PendingCapture {
    ComPtr<IDirect3DSurface9> surface;
    ComPtr<IDirect3DQuery9>   query;
    int64_t                   pts = 0;
};

class SurfacePool {
public:
    explicit SurfacePool(Logger& logger);
    ~SurfacePool() = default;
    SurfacePool(const SurfacePool&) = delete;
    SurfacePool& operator=(const SurfacePool&) = delete;

    bool initialize(IDirect3DDevice9* device, UINT width, UINT height,
                    D3DFORMAT format, int count = 6);
    void reset();

    ComPtr<IDirect3DSurface9> acquire();
    void release(ComPtr<IDirect3DSurface9> surface);

    bool empty()    const;
    bool needsReinit(UINT width, UINT height, D3DFORMAT format) const;

    UINT      cachedWidth()  const { return m_cachedWidth;  }
    UINT      cachedHeight() const { return m_cachedHeight; }
    D3DFORMAT cachedFormat() const { return m_cachedFormat; }

private:
    Logger&    m_logger;
    mutable std::mutex m_mutex;
    std::vector<ComPtr<IDirect3DSurface9>> m_pool;

    UINT      m_cachedWidth  = 0;
    UINT      m_cachedHeight = 0;
    D3DFORMAT m_cachedFormat = D3DFMT_UNKNOWN;
};
