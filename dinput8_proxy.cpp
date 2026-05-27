/*
 * dinput8_proxy.cpp
 * Proxy dinput8.dll for NFS The Run — scales down FFB effect magnitudes.
 * Drop the compiled dinput8.dll into the game folder.
 * Tune settings in dinput8_proxy.ini next to the game exe.
 *
 * Build: 32-bit MSVC (x86 Release)
 */

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstdio>

// ============================================================
//  Config (read from dinput8_proxy.ini)
// ============================================================
static float g_gainScale      = 0.30f;   // overall FFB strength (0.0 - 1.0)
static float g_springScale     = 0.15f;   // extra scale applied to spring/damper effects
static float g_constantScale   = 0.40f;   // scale for constant-force effects
static float g_periodicScale   = 0.40f;   // scale for sine/square/triangle effects

static char g_iniPath[MAX_PATH] = {};

static void BuildIniPath()
{
    if (g_iniPath[0]) return;
    GetModuleFileNameA(NULL, g_iniPath, MAX_PATH);
    char* slash = strrchr(g_iniPath, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(g_iniPath, "dinput8_proxy.ini");
}

// Saves current FFB values to INI
static void SaveConfig()
{
    BuildIniPath();
    char buf[32];
    auto writeFloat = [&](const char* key, float val) {
        sprintf_s(buf, "%.2f", val);
        WritePrivateProfileStringA("FFB", key, buf, g_iniPath);
    };
    writeFloat("GainScale",     g_gainScale);
    writeFloat("SpringScale",   g_springScale);
    writeFloat("ConstantScale", g_constantScale);
    writeFloat("PeriodicScale", g_periodicScale);
}

static void LoadConfig()
{
    BuildIniPath();

    auto readFloat = [&](const char* key, float def) -> float {
        char buf[32];
        GetPrivateProfileStringA("FFB", key, nullptr, buf, sizeof(buf), g_iniPath);
        if (buf[0] == '\0') return def;
        return static_cast<float>(atof(buf));
    };

    g_gainScale     = readFloat("GainScale",     g_gainScale);
    g_springScale   = readFloat("SpringScale",   g_springScale);
    g_constantScale = readFloat("ConstantScale", g_constantScale);
    g_periodicScale = readFloat("PeriodicScale", g_periodicScale);

    // Clamp to sane range
    auto clamp01 = [](float v) -> float {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    };
    g_gainScale     = clamp01(g_gainScale);
    g_springScale   = clamp01(g_springScale);
    g_constantScale = clamp01(g_constantScale);
    g_periodicScale = clamp01(g_periodicScale);
}

#include "overlay.h"

// ============================================================
//  Real dinput8.dll — loaded from System32
// ============================================================
static HMODULE g_hRealDInput8 = nullptr;

typedef HRESULT(WINAPI* PFN_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static PFN_DirectInput8Create g_realDI8Create = nullptr;

// ============================================================
//  Forward declarations
// ============================================================
struct ProxyDirectInputDevice8A;
struct ProxyDirectInputEffect;

// ============================================================
//  ProxyDirectInputEffect
//  Wraps IDirectInputEffect and scales magnitudes on Download/SetParameters
// ============================================================
struct ProxyDirectInputEffect : public IDirectInputEffect
{
    IDirectInputEffect* m_real;
    GUID                m_effGuid;   // GUID_Spring, GUID_ConstantForce, etc.
    LONG                m_refCount;

    ProxyDirectInputEffect(IDirectInputEffect* real, REFGUID effGuid)
        : m_real(real), m_effGuid(effGuid), m_refCount(1) {}

    // ---- Determine per-type scale ----
    float EffectScale() const
    {
        if (m_effGuid == GUID_Spring   || m_effGuid == GUID_Damper  ||
            m_effGuid == GUID_Inertia  || m_effGuid == GUID_Friction)
            return g_springScale;
        if (m_effGuid == GUID_ConstantForce)
            return g_constantScale;
        // Periodic: sine, square, triangle, sawtooth
        return g_periodicScale;
    }

    // ---- Scale a DIEFFECT before forwarding ----
    void ScaleEffect(DIEFFECT& eff)
    {
        // Scale the global gain field (DIEFF_GAIN = 0x00000004)
        if (eff.dwFlags & 0x00000004)
        {
            DWORD scaled = static_cast<DWORD>(eff.dwGain * g_gainScale);
            eff.dwGain = scaled < (DWORD)DI_FFNOMINALMAX ? scaled : (DWORD)DI_FFNOMINALMAX;
        }

        float typeScale = EffectScale();

        // Scale type-specific magnitude
        if (eff.lpvTypeSpecificParams && eff.cbTypeSpecificParams > 0)
        {
            if (m_effGuid == GUID_ConstantForce &&
                eff.cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
            {
                auto* cf = reinterpret_cast<DICONSTANTFORCE*>(eff.lpvTypeSpecificParams);
                cf->lMagnitude = static_cast<LONG>(cf->lMagnitude * typeScale);
            }
            else if ((m_effGuid == GUID_Spring  || m_effGuid == GUID_Damper ||
                      m_effGuid == GUID_Inertia || m_effGuid == GUID_Friction) &&
                     eff.cbTypeSpecificParams >= sizeof(DICONDITION))
            {
                // Condition effects have one DICONDITION per axis
                DWORD count = eff.cbTypeSpecificParams / sizeof(DICONDITION);
                auto* cond  = reinterpret_cast<DICONDITION*>(eff.lpvTypeSpecificParams);
                for (DWORD i = 0; i < count; ++i)
                {
                    cond[i].lPositiveCoefficient = static_cast<LONG>(cond[i].lPositiveCoefficient * typeScale);
                    cond[i].lNegativeCoefficient = static_cast<LONG>(cond[i].lNegativeCoefficient * typeScale);
                    // dwPositiveSaturation / dwNegativeSaturation are DWORD in this SDK
                    cond[i].dwPositiveSaturation = static_cast<DWORD>(cond[i].dwPositiveSaturation * typeScale);
                    cond[i].dwNegativeSaturation = static_cast<DWORD>(cond[i].dwNegativeSaturation * typeScale);
                }
            }
            else if (eff.cbTypeSpecificParams >= sizeof(DIPERIODIC))
            {
                // Periodic (sine, square, etc.)
                auto* per = reinterpret_cast<DIPERIODIC*>(eff.lpvTypeSpecificParams);
                per->dwMagnitude = static_cast<DWORD>(per->dwMagnitude * typeScale);
            }
        }
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDirectInputEffect) {
            *ppv = this; AddRef(); return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--m_refCount == 0) { m_real->Release(); delete this; return 0; }
        return m_refCount;
    }

    // ---- IDirectInputEffect ----
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v, REFGUID g) override
        { return m_real->Initialize(h, v, g); }

    HRESULT STDMETHODCALLTYPE GetEffectGuid(LPGUID pg) override
        { return m_real->GetEffectGuid(pg); }

    HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT peff, DWORD flags) override
        { return m_real->GetParameters(peff, flags); }

    HRESULT STDMETHODCALLTYPE SetParameters(LPCDIEFFECT peff, DWORD flags) override
    {
        if (!peff) return m_real->SetParameters(peff, flags);

        // Copy the struct so we can mutate it safely
        DIEFFECT copy = *peff;
        ScaleEffect(copy);
        return m_real->SetParameters(&copy, flags);
    }

    HRESULT STDMETHODCALLTYPE Start(DWORD iter, DWORD flags) override
        { return m_real->Start(iter, flags); }

    HRESULT STDMETHODCALLTYPE Stop() override
        { return m_real->Stop(); }

    HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD pdw) override
        { return m_real->GetEffectStatus(pdw); }

    HRESULT STDMETHODCALLTYPE Download() override
        { return m_real->Download(); }

    HRESULT STDMETHODCALLTYPE Unload() override
        { return m_real->Unload(); }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pe) override
        { return m_real->Escape(pe); }
};

// ============================================================
//  ProxyDirectInputDevice8A
//  Wraps IDirectInputDevice8A — intercepts CreateEffect
// ============================================================
struct ProxyDirectInputDevice8A : public IDirectInputDevice8A
{
    IDirectInputDevice8A* m_real;
    LONG                  m_refCount;

    ProxyDirectInputDevice8A(IDirectInputDevice8A* real)
        : m_real(real), m_refCount(1) {}

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A) {
            *ppv = this; AddRef(); return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--m_refCount == 0) { m_real->Release(); delete this; return 0; }
        return m_refCount;
    }

    // ---- IDirectInputDevice8A — pass-through for everything except CreateEffect ----
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS p)              override { return m_real->GetCapabilities(p); }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD f) override { return m_real->EnumObjects(cb, ref, f); }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID g, LPDIPROPHEADER p)    override { return m_real->GetProperty(g, p); }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID g, LPCDIPROPHEADER p)   override { return m_real->SetProperty(g, p); }
    HRESULT STDMETHODCALLTYPE Acquire()                                    override { return m_real->Acquire(); }
    HRESULT STDMETHODCALLTYPE Unacquire()                                  override { return m_real->Unacquire(); }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD s, LPVOID d)           override { return m_real->GetDeviceState(s, d); }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD s, LPDIDEVICEOBJECTDATA d, LPDWORD n, DWORD f) override { return m_real->GetDeviceData(s, d, n, f); }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT p)             override { return m_real->SetDataFormat(p); }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE h)              override { return m_real->SetEventNotification(h); }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND h, DWORD f)        override { return m_real->SetCooperativeLevel(h, f); }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA p, DWORD o, DWORD h) override { return m_real->GetObjectInfo(p, o, h); }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA p)        override { return m_real->GetDeviceInfo(p); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD f)            override { return m_real->RunControlPanel(h, f); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v, REFGUID g) override { return m_real->Initialize(h, v, g); }

    // ---- THE KEY INTERCEPT ----
    HRESULT STDMETHODCALLTYPE CreateEffect(
        REFGUID rguid, LPCDIEFFECT peff,
        LPDIRECTINPUTEFFECT* ppeff, LPUNKNOWN pOuter) override
    {
        // Scale parameters before the effect is created
        DIEFFECT scaled = {};
        if (peff) {
            scaled = *peff;
            // Reuse the proxy scale logic via a temporary object
            ProxyDirectInputEffect tmp(nullptr, rguid);
            tmp.ScaleEffect(scaled);
            peff = &scaled;
        }

        IDirectInputEffect* realEffect = nullptr;
        HRESULT hr = m_real->CreateEffect(rguid, peff, &realEffect, pOuter);
        if (SUCCEEDED(hr) && realEffect)
            *ppeff = new ProxyDirectInputEffect(realEffect, rguid);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA cb, LPVOID ref, DWORD t) override { return m_real->EnumEffects(cb, ref, t); }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA p, REFGUID g)  override { return m_real->GetEffectInfo(p, g); }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD p)             override { return m_real->GetForceFeedbackState(p); }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD f)            override { return m_real->SendForceFeedbackCommand(f); }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD f) override { return m_real->EnumCreatedEffectObjects(cb, ref, f); }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE p)                      override { return m_real->Escape(p); }
    HRESULT STDMETHODCALLTYPE Poll()                                       override { return m_real->Poll(); }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD s, LPCDIDEVICEOBJECTDATA d, LPDWORD n, DWORD f) override { return m_real->SendDeviceData(s, d, n, f); }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR fn, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD f) override { return m_real->EnumEffectsInFile(fn, cb, ref, f); }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR fn, DWORD n, LPDIFILEEFFECT e, DWORD f)    override { return m_real->WriteEffectToFile(fn, n, e, f); }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA p, LPCSTR u, DWORD f) override { return m_real->BuildActionMap(p, u, f); }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA p, LPCSTR u, DWORD f)  override { return m_real->SetActionMap(p, u, f); }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA p)   override { return m_real->GetImageInfo(p); }
};

// ============================================================
//  ProxyDirectInput8A — wraps IDirectInput8A, intercepts CreateDevice
// ============================================================
struct ProxyDirectInput8A : public IDirectInput8A
{
    IDirectInput8A* m_real;
    LONG            m_refCount;

    ProxyDirectInput8A(IDirectInput8A* real) : m_real(real), m_refCount(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
            *ppv = this; AddRef(); return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return ++m_refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--m_refCount == 0) { m_real->Release(); delete this; return 0; }
        return m_refCount;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8A* ppdev, LPUNKNOWN pOuter) override
    {
        IDirectInputDevice8A* realDev = nullptr;
        HRESULT hr = m_real->CreateDevice(rguid, &realDev, pOuter);
        if (SUCCEEDED(hr) && realDev)
            *ppdev = new ProxyDirectInputDevice8A(realDev);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD t, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD f) override { return m_real->EnumDevices(t, cb, ref, f); }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g)                  override { return m_real->GetDeviceStatus(g); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD f)            override { return m_real->RunControlPanel(h, f); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v)            override { return m_real->Initialize(h, v); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID g, LPCSTR n, LPGUID pg)  override { return m_real->FindDevice(g, n, pg); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR u, LPDIACTIONFORMATA p, LPDIENUMDEVICESBYSEMANTICSCBA cb, LPVOID ref, DWORD f) override { return m_real->EnumDevicesBySemantics(u, p, cb, ref, f); }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK cb, LPDICONFIGUREDEVICESPARAMSA p, DWORD f, LPVOID ref) override { return m_real->ConfigureDevices(cb, p, f, ref); }
};

// ============================================================
//  Exported DirectInput8Create — the only export the game calls
// ============================================================
extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
    LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    if (!g_realDI8Create) return E_FAIL;

    // We only proxy the ANSI interface (A variant) which games typically use
    if (riidltf == IID_IDirectInput8A)
    {
        IDirectInput8A* real = nullptr;
        HRESULT hr = g_realDI8Create(hinst, dwVersion, riidltf, (LPVOID*)&real, punkOuter);
        if (SUCCEEDED(hr) && real)
            *ppvOut = new ProxyDirectInput8A(real);
        return hr;
    }

    // Wide (W) interface — pass through unmodified (rare in old games)
    return g_realDI8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

// ============================================================
//  DllMain
// ============================================================
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinstDLL);
        LoadConfig();

        // Load the REAL dinput8.dll from System32
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        strcat_s(sysPath, "\\dinput8.dll");
        g_hRealDInput8 = LoadLibraryA(sysPath);
        if (!g_hRealDInput8) return FALSE;

        g_realDI8Create = reinterpret_cast<PFN_DirectInput8Create>(
            GetProcAddress(g_hRealDInput8, "DirectInput8Create"));
        if (!g_realDI8Create) return FALSE;

        HookDXGIPresent();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        ShutdownOverlay();
        if (g_hRealDInput8) FreeLibrary(g_hRealDInput8);
    }
    return TRUE;
}
