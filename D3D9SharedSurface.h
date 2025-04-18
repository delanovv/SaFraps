#pragma once
#include <d3d9.h>
#include <d3d9types.h>
#include <windows.h>
#include <string>
#pragma comment(lib, "d3d9.lib")

class D3D9SharedSurface {
public:
    D3D9SharedSurface();
    ~D3D9SharedSurface();

    bool initialize(IDirect3DDevice9* gameDevice, int width, int height);
    HANDLE getSharedHandle() const { return m_sharedHandle; }
    IDirect3DSurface9* getSurface() const { return m_sharedSurface; }
    bool copyFromBackbuffer(IDirect3DDevice9* gameDevice, const RECT* pSrcRect);
private:
    IDirect3DTexture9* m_sharedTexture = nullptr;
    IDirect3DSurface9* m_sharedSurface = nullptr;
    IDirect3DSurface9* m_intermediateSurface = nullptr; // Для мультисэмплинга
    HANDLE m_sharedHandle = nullptr;

    int m_width = 0;
    int m_height = 0;
};