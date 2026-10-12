// Clear / BeginScene / EndScene hooks and device hook installation. (part of the unity build, see src/main.cpp)
typedef HRESULT(STDMETHODCALLTYPE* PFN_Clear)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Scene)(IDirect3DDevice9*);
static PFN_Clear oClear;
static PFN_Scene oBeginScene, oEndScene;

static HRESULT STDMETHODCALLTYPE hkBeginScene(IDirect3DDevice9* d)
{
    HRESULT hr = oBeginScene(d);
    if (!g_inFX && SUCCEEDED(hr)) g_inScene = true;
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkEndScene(IDirect3DDevice9* d)
{
    if (!g_inFX) g_inScene = false;
    return oEndScene(d);
}

// The game clears depth after the world, before the weapon: last chance for AO/fog.
static HRESULT STDMETHODCALLTYPE hkClear(IDirect3DDevice9* d, DWORD n, const D3DRECT* r, DWORD flags,
                                         D3DCOLOR c, float z, DWORD st)
{
    if (!g_inFX && (flags & D3DCLEAR_ZBUFFER) && g_drawsSince3D > 0 && !g_depthDone && !g_fxDone && S.enabled) {
        g_inFX = true;
        RunFX(d, STAGE_DEPTH);
        g_inFX = false;
        g_depthDone = true;
        g_depthStages++;
    }
    if (!g_inFX && (flags & D3DCLEAR_ZBUFFER) && g_depthTex && S.wpn) {
        flags |= D3DCLEAR_STENCIL;
        st = 0;
    }
    return oClear(d, n, r, flags, c, z, st);
}

static void InstallHooks(IDirect3DDevice9* dev)
{
    if (void* o = HookVtable(dev, 41, (void*)hkBeginScene)) oBeginScene = (PFN_Scene)o;
    if (void* o = HookVtable(dev, 42, (void*)hkEndScene))   oEndScene = (PFN_Scene)o;
    if (void* o = HookVtable(dev, 43, (void*)hkClear))      oClear = (PFN_Clear)o;
    if (void* o = HookVtable(dev, 51, (void*)hkSetLight))   oSetLight = (PFN_SetLight)o;
    if (void* o = HookVtable(dev, 91, (void*)hkCreateVS))   oCreateVS = (PFN_CreateVS)o;
    if (void* o = HookVtable(dev, 94, (void*)hkSetVSConstF)) oSetVSConstF = (PFN_SetVSConstF)o;
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    g_qpcMs = 1000.0 / (double)fq.QuadPart;
    if (g_bufFix || g_profile) {
        if (void* o = HookVtable(dev, 26, (void*)hkCVB)) oCVB = (PFN_CVB)o;
        if (void* o = HookVtable(dev, 27, (void*)hkCIB)) oCIB = (PFN_CIB)o;
    }
    if (void* o = HookVtable(dev, 81, (void*)hkDP))  oDP = (PFN_DP)o;
    if (void* o = HookVtable(dev, 82, (void*)hkDIP)) oDIP = (PFN_DIP)o;
    if (void* o = HookVtable(dev, 44, (void*)hkSetTransform)) oSetTransform = (PFN_SetTransform)o;
    if (void* o = HookVtable(dev, 83, (void*)hkDPUP))  oDPUP = (PFN_DPUP)o;
    if (void* o = HookVtable(dev, 84, (void*)hkDIPUP)) oDIPUP = (PFN_DIPUP)o;
    if (g_profile) {
        if (void* o = HookVtable(dev, 23, (void*)hkCTex))  oCTex = (PFN_CTex)o;
        Log("Perfilador activo: se registran los fotogramas lentos");
    }
    if (g_bufFix) Log("Arreglo de buffers activo");
}

