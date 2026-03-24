#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "zlib.lib")
#pragma comment(lib, "libx264.lib")
#pragma comment(lib, "x265-static.lib")
#pragma comment(lib, "nvcuvid.lib")
#pragma comment(lib, "nvencodeapi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "libmfx.lib")
#pragma comment(lib, "snappy.lib")
#pragma comment(lib, "OpenCL.lib")
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "libxml2.lib")
#pragma comment(lib, "lzma.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "opus.lib")
#pragma comment(lib, "bz2.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "dxva2.lib")
#include <Windows.h>
#include <string>
#include <iostream>
#include <cstring>
#include <string>
#include "SAMP/SAMP.hpp"
#include "ImGUI/imgui.h"
#include "ImGUI/imgui_impl_dx9.h"
#include "ImGUI/imgui_impl_win32.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);