// DrawPrimitiveUP hooks and resource creation hooks. (part of the unity build, see src/main.cpp)
static HRESULT STDMETHODCALLTYPE hkDPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT n, const void* v, UINT st)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DPUP", t, n, v, st, 0, 0, 0, 0);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, false, t, 0, 0, 0, 0, n, v, st); AfterDraw(d); return D3D_OK; }
    RecordDrawUP(d, false, t, 0, 0, n, nullptr, D3DFMT_UNKNOWN, v, st);
    if (!g_inFX) NoteCursor(d, n, v, st);
    HudLogDraw(d, "DPUP", t, n, v, st, 0, VertsForPrims(t, n));
    WorldLogDraw(d, "DPUP", t, n, v, st, 0);
    if (HudLayoutDraw(d, false, true, t, 0, 0, 0, 0, n, nullptr, D3DFMT_UNKNOWN, v, st)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDPUP(d, t, n, v, st);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.drawsUP++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDIPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT mn, UINT nv, UINT n,
                                         const void* i, D3DFORMAT f, const void* v, UINT st)
{
    BeforeDraw(d);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, true, t, 0, mn, nv, 0, n, v, st); AfterDraw(d); return D3D_OK; }
    RecordDrawUP(d, true, t, mn, nv, n, i, f, v, st);
    HudLogDraw(d, "DIPUP", t, n, v, st, mn, nv);
    WorldLogDraw(d, "DIPUP", t, n, v, st, mn);
    if (HudLayoutDraw(d, true, true, t, 0, mn, nv, 0, n, i, f, v, st)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDIPUP(d, t, mn, nv, n, i, f, v, st);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.drawsUP++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCTex(IDirect3DDevice9* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p,
                                        IDirect3DTexture9** out, HANDLE* sh)
{
    double t0 = NowMs();
    HRESULT hr = oCTex(d, w, h, l, u, f, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 19, (void*)hkTexLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCVB(IDirect3DDevice9* d, UINT len, DWORD u, DWORD fvf, D3DPOOL p,
                                       IDirect3DVertexBuffer9** out, HANDLE* sh)
{
    double t0 = NowMs();
    FixBufferPool(u, p);
    HRESULT hr = oCVB(d, len, u, fvf, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 11, (void*)hkVBLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCIB(IDirect3DDevice9* d, UINT len, DWORD u, D3DFORMAT f, D3DPOOL p,
                                       IDirect3DIndexBuffer9** out, HANDLE* sh)
{
    double t0 = NowMs();
    FixBufferPool(u, p);
    HRESULT hr = oCIB(d, len, u, f, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 11, (void*)hkIBLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

