#pragma once
// overlay.h — hooks D3D11CreateDevice + IDXGIFactory::CreateSwapChain
// using MinHook to intercept Present. No ImGui for now.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <cstdarg>
#include "MinHook.h"

extern float g_gainScale;
extern float g_springScale;
extern float g_constantScale;
extern float g_periodicScale;

// ============================================================
//  Log
// ============================================================
static FILE* g_logFile = nullptr;
static void Log(const char* fmt, ...)
{
    if (!g_logFile) return;
    va_list a; va_start(a, fmt);
    vfprintf(g_logFile, fmt, a);
    va_end(a);
    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}
static void OpenLog()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* sl = strrchr(path, '\\');
    if (sl) *(sl+1) = '\0';
    strcat_s(path, "dinput8_proxy_log.txt");
    fopen_s(&g_logFile, path, "w");
    Log("=== dinput8_proxy log ===");
}

// ============================================================
//  State
// ============================================================
static ID3D11Device*        g_pd3dDevice  = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;

typedef HRESULT(WINAPI* PFN_Present)(IDXGISwapChain*, UINT, UINT);
static PFN_Present g_origPresent = nullptr;

typedef HRESULT(WINAPI* PFN_CreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
static PFN_CreateSwapChain g_origCreateSwapChain = nullptr;

typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
static PFN_D3D11CreateDevice g_origD3D11CreateDevice = nullptr;

// ============================================================
//  Hooked Present — placeholder, just calls through for now
// ============================================================
static HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static int n = 0;
    if (++n <= 3) Log("HookedPresent called #%d", n);
    return g_origPresent(pSwapChain, SyncInterval, Flags);
}

// ============================================================
//  Hooked IDXGIFactory::CreateSwapChain
// ============================================================
static HRESULT WINAPI HookedCreateSwapChain(
    IDXGIFactory* pFactory, IUnknown* pDevice,
    DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    Log("HookedCreateSwapChain called");
    HRESULT hr = g_origCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && !g_origPresent)
    {
        Log("Got swapchain — hooking Present via MinHook");
        void** vmt = *reinterpret_cast<void***>(*ppSwapChain);
        void* pPresent = vmt[8];  // IDXGISwapChain::Present is slot 8

        if (MH_CreateHook(pPresent, &HookedPresent, reinterpret_cast<void**>(&g_origPresent)) == MH_OK)
            MH_EnableHook(pPresent);
        Log("Present hook: %s", g_origPresent ? "OK" : "FAILED");
    }
    return hr;
}

// ============================================================
//  Hooked D3D11CreateDevice
// ============================================================
static HRESULT WINAPI HookedD3D11CreateDevice(
    IDXGIAdapter*            pAdapter,
    D3D_DRIVER_TYPE          DriverType,
    HMODULE                  Software,
    UINT                     Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT                     FeatureLevels,
    UINT                     SDKVersion,
    ID3D11Device**           ppDevice,
    D3D_FEATURE_LEVEL*       pFeatureLevel,
    ID3D11DeviceContext**    ppImmediateContext)
{
    Log("HookedD3D11CreateDevice called");
    HRESULT hr = g_origD3D11CreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !g_pd3dDevice)
    {
        g_pd3dDevice  = *ppDevice;         g_pd3dDevice->AddRef();
        g_pd3dContext = *ppImmediateContext; g_pd3dContext->AddRef();
        Log("Got device — hooking IDXGIFactory::CreateSwapChain");

        IDXGIDevice*  dxgiDev     = nullptr;
        IDXGIAdapter* dxgiAdapter = nullptr;
        IDXGIFactory* dxgiFactory = nullptr;

        if (SUCCEEDED((*ppDevice)->QueryInterface(__uuidof(IDXGIDevice),  (void**)&dxgiDev))
         && SUCCEEDED(dxgiDev->GetAdapter(&dxgiAdapter))
         && SUCCEEDED(dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory)))
        {
            void** vmt = *reinterpret_cast<void***>(dxgiFactory);
            void* pCreateSwapChain = vmt[10]; // IDXGIFactory::CreateSwapChain slot 10

            MH_STATUS s = MH_CreateHook(pCreateSwapChain, &HookedCreateSwapChain,
                                        reinterpret_cast<void**>(&g_origCreateSwapChain));
            if (s == MH_OK) MH_EnableHook(pCreateSwapChain);
            Log("CreateSwapChain hook: %s (MH status %d)", g_origCreateSwapChain ? "OK" : "FAILED", s);
        }
        else Log("Failed to get IDXGIFactory");

        if (dxgiFactory) dxgiFactory->Release();
        if (dxgiAdapter) dxgiAdapter->Release();
        if (dxgiDev)     dxgiDev->Release();
    }

    Log("HookedD3D11CreateDevice returning hr=0x%08X", hr);
    return hr;
}

// ============================================================
//  Entry point — hook D3D11CreateDevice via MinHook
// ============================================================
static void HookDXGIPresent()
{
    OpenLog();
    Log("HookDXGIPresent called — using MinHook");

    MH_Initialize();

    HMODULE hD3D11 = LoadLibraryA("d3d11.dll");
    void* pTarget  = GetProcAddress(hD3D11, "D3D11CreateDevice");
    Log("D3D11CreateDevice at 0x%p", pTarget);

    MH_STATUS s = MH_CreateHook(pTarget, &HookedD3D11CreateDevice,
                                 reinterpret_cast<void**>(&g_origD3D11CreateDevice));
    if (s == MH_OK) MH_EnableHook(pTarget);
    Log("D3D11CreateDevice hook: %s (MH status %d)", g_origD3D11CreateDevice ? "OK" : "FAILED", s);
}

static void ShutdownOverlay()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice)  { g_pd3dDevice->Release();  g_pd3dDevice  = nullptr; }
}
