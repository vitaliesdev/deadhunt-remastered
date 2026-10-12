// DrawPrimitive / DrawIndexedPrimitive hooks. (part of the unity build, see src/main.cpp)
static HRESULT STDMETHODCALLTYPE hkDP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT n)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DP", t, n, nullptr, 0, 0, 0, 0, s);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, false, t, 0, 0, 0, s, n, nullptr, 0); AfterDraw(d); return D3D_OK; }
    if (IsGameCrosshair(d, t, s, n)) { AfterDraw(d); return D3D_OK; }
    RecordDraw(d, false, t, 0, 0, 0, s, n);
    HudLogDraw(d, "DP", t, n, nullptr, 0, s, VertsForPrims(t, n));
    WorldLogDraw(d, "DP", t, n, nullptr, 0, s);
    if (HudLayoutDraw(d, false, false, t, 0, 0, 0, s, n, nullptr, D3DFMT_UNKNOWN, nullptr, 0)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDP(d, t, s, n);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.draws++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDIP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mn, UINT nv, UINT si, UINT n)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DIP", t, n, nullptr, 0, b, mn, nv, 0);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, true, t, b, mn, nv, si, n, nullptr, 0); AfterDraw(d); return D3D_OK; }
    RecordDraw(d, true, t, b, mn, nv, si, n);
    HudLogDraw(d, "DIP", t, n, nullptr, 0, (UINT)(b + (INT)mn), nv);
    WorldLogDraw(d, "DIP", t, n, nullptr, 0, (UINT)(b + (INT)mn));
    if (AdvDraw(d, t, b, mn, nv, si, n)) return D3D_OK;
    bool dim = false, artBg = false;
    if (MenuArtDraw(d, b, mn, nv, &artBg)) {
        AfterDraw(d);
        if (artBg && SkinOn()) SkinBackgroundDim(d);
        return D3D_OK;
    }
    if (SkinDraw(d, true, t, b, mn, nv, si, n, &dim)) { AfterDraw(d); return D3D_OK; }
    if (HudLayoutDraw(d, true, false, t, b, mn, nv, si, n, nullptr, D3DFMT_UNKNOWN, nullptr, 0)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    bool terr = TerrainBegin(d);
    HRESULT hr = oDIP(d, t, b, mn, nv, si, n);
    if (terr) TerrainEnd(d);
    AfterDraw(d);
    if (dim) SkinBackgroundDim(d);
    WeaponIconDraw(d, t, b, mn, nv, si, n);
    if (!g_inFX && g_profile) { F.draws++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

