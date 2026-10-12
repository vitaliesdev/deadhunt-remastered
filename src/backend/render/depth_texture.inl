// Readable scene depth (INTZ) replacing the game's depth buffer. (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- depth texture

static void ReleaseDepth()
{
    if (g_depthSurf) { g_depthSurf->Release(); g_depthSurf = nullptr; }
    if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
}

static void CreateDepth(IDirect3DDevice9* dev)
{
    ReleaseDepth();
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    bb->Release();
    if (SUCCEEDED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_DEPTHSTENCIL, FMT_INTZ, D3DPOOL_DEFAULT,
                                     &g_depthTex, nullptr)) &&
        SUCCEEDED(g_depthTex->GetSurfaceLevel(0, &g_depthSurf))) {
        dev->SetDepthStencilSurface(g_depthSurf);
        Log("Profundidad legible (INTZ) %ux%u", d.Width, d.Height);
        return;
    }
    ReleaseDepth();
    // Fallback: plain depth buffer so the game keeps working; depth effects stay off.
    if (SUCCEEDED(dev->CreateDepthStencilSurface(d.Width, d.Height, g_depthFallbackFmt, D3DMULTISAMPLE_NONE, 0,
                                                 TRUE, &g_depthSurf, nullptr)))
        dev->SetDepthStencilSurface(g_depthSurf);
    Log("INTZ no disponible: oclusion ambiental y niebla desactivadas");
}

