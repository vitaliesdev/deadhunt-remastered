// Render targets and GPU resources of the effect chain. (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- render targets

struct RT {
    IDirect3DTexture9* tex = nullptr;
    IDirect3DSurface9* surf = nullptr;
    UINT w = 0, h = 0;
};
static RT g_scene, g_aa, g_q1, g_q2, g_e1, g_e2, g_edges, g_weights, g_ao1, g_ao2;
static RT g_ray1, g_ray2, g_dof1, g_dof2;   // quarter resolution
static RT g_worldZ;                         // world linear depth, saved before the game clears it
static RT g_fl32, g_fl4, g_fl1;             // muzzle flash detection: 32x16 -> 4x2 -> 1x1
static RT g_handMask;                       // 1 where the hands were drawn (from stencil tags)
static RT g_sh1, g_sh2;                     // screen-space sun shadows (half resolution)
static bool RenderShadowMap(IDirect3DDevice9* dev);
static void SetShadowParams(IDirect3DDevice9* dev);
static void ReleaseShadowMap();
static void ReleaseShadowDraws();
static IDirect3DTexture9* g_smTex;      // shadow map (INTZ), near cascade
static IDirect3DTexture9* g_smTex2;     // far cascade
static bool g_lightOk;

static bool g_worldZValid;                  // g_worldZ was written this frame

// Readable scene depth: the game's auto depth buffer is replaced by an INTZ texture.
static const D3DFORMAT FMT_INTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
static IDirect3DTexture9* g_depthTex;
static IDirect3DSurface9* g_depthSurf;
static bool g_depthWanted;      // we disabled the auto depth buffer and own the depth surface
static D3DFORMAT g_depthFallbackFmt = D3DFMT_D24S8;

// Camera captured from SetTransform (main 3D projection of the current frame).
static float g_zn = 50, g_zf = 100000, g_invP11 = 1, g_invP22 = 1;
static float g_up[4];           // world-up row of the inverse view matrix
static D3DMATRIX g_mainView;    // view matrix of the main camera
static float g_wzn = 10, g_wzf = 500, g_winvP11 = 0, g_winvP22 = 0.3f;   // weapon camera
static float g_sunView[3];      // direction towards the sun in view space
static bool g_sunInView;
static bool g_haveSun;          // a directional light (the sun) was captured
static float g_sunDir[3], g_sunCol[3] = { 1, 0.95f, 0.85f };
static D3DMATRIX g_lastView;
static D3DMATRIX g_curProjM;     // projection currently set (any)
static bool g_curProjMain, g_have3D, g_fxDone, g_depthDone, g_inScene;   // g_have3D: a main camera was captured (persistent)
static int g_drawsSince3D;                        // world draws this frame (non screen-space)
static int g_hudByFvf, g_hudByProj, g_depthStages;
static IDirect3DTexture9 *g_areaTex, *g_searchTex;   // MANAGED: survive Reset
static IDirect3DStateBlock9* g_sb;
static UINT g_w, g_h;
static bool g_resOk;

static void FreeRT(RT& r)
{
    if (r.surf) r.surf->Release();
    if (r.tex) r.tex->Release();
    r = RT();
}

static void ReleaseResources()
{
    FreeRT(g_scene); FreeRT(g_aa); FreeRT(g_q1); FreeRT(g_q2); FreeRT(g_e1); FreeRT(g_e2);
    FreeRT(g_edges); FreeRT(g_weights); FreeRT(g_ao1); FreeRT(g_ao2);
    FreeRT(g_ray1); FreeRT(g_ray2); FreeRT(g_dof1); FreeRT(g_dof2);
    FreeRT(g_worldZ); FreeRT(g_fl32); FreeRT(g_fl4); FreeRT(g_fl1); FreeRT(g_handMask);
    FreeRT(g_sh1); FreeRT(g_sh2);
    ReleaseShadowMap();
    if (g_sb) { g_sb->Release(); g_sb = nullptr; }
    g_resOk = false;
    g_w = g_h = 0;
}

static bool MakeRT(IDirect3DDevice9* dev, RT& r, UINT w, UINT h, D3DFORMAT fmt, D3DFORMAT fallback)
{
    r.w = w ? w : 1;
    r.h = h ? h : 1;
    if (FAILED(dev->CreateTexture(r.w, r.h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &r.tex, nullptr)) &&
        FAILED(dev->CreateTexture(r.w, r.h, 1, D3DUSAGE_RENDERTARGET, fallback, D3DPOOL_DEFAULT, &r.tex, nullptr)))
        return false;
    return SUCCEEDED(r.tex->GetSurfaceLevel(0, &r.surf));
}

static IDirect3DTexture9* MakeLookup(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt,
                                     const unsigned char* bytes, UINT pitch)
{
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED, &t, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
        for (UINT y = 0; y < h; y++)
            memcpy((unsigned char*)lr.pBits + y * lr.Pitch, bytes + y * pitch, pitch);
        t->UnlockRect(0);
    }
    return t;
}

static bool CreateResources(IDirect3DDevice9* dev, const D3DSURFACE_DESC& bb)
{
    // SMAA AreaTex is R8G8 -> A8L8 (red in L, green in A: read as .ra in SMAA_HLSL_3)
    if (!g_areaTex)
        g_areaTex = MakeLookup(dev, AREATEX_WIDTH, AREATEX_HEIGHT, D3DFMT_A8L8, areaTexBytes, AREATEX_PITCH);
    if (!g_searchTex)
        g_searchTex = MakeLookup(dev, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, D3DFMT_L8, searchTexBytes, SEARCHTEX_PITCH);
    ReleaseResources();
    g_w = bb.Width;
    g_h = bb.Height;
    // patch_label.py: Options shows this when the resolution is not in the monitor's mode list
    {
        static const unsigned char cave[6] = { 0x8b, 0x45, 0xf0, 0x8b, 0x48, 0x30 };
        char* label = (char*)0x676260;
        if (!IsBadReadPtr((void*)0x676240, 0x30) && !memcmp((void*)0x676240, cave, 6)) {
            DWORD old;
            VirtualProtect(label, 16, PAGE_EXECUTE_READWRITE, &old);
            sprintf_s(label, 16, "%ux%u", g_w, g_h);
            VirtualProtect(label, 16, old, &old);
        }
    }
    const D3DFORMAT hdr = D3DFMT_A16B16G16R16F;
    bool ok = MakeRT(dev, g_scene, g_w, g_h, bb.Format, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_aa, g_w, g_h, D3DFMT_A8R8G8B8, bb.Format)
           && MakeRT(dev, g_q1, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_q2, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_e1, g_w / 16, g_h / 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_e2, g_w / 16, g_h / 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_edges, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_weights, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ao1, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ao2, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ray1, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ray2, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_dof1, g_w / 4, g_h / 4, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_dof2, g_w / 4, g_h / 4, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_worldZ, g_w, g_h, D3DFMT_R32F, hdr)
           && MakeRT(dev, g_fl32, 32, 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_fl4, 4, 2, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_fl1, 1, 1, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_handMask, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_sh1, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_sh2, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && g_areaTex && g_searchTex
           && SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &g_sb));
    if (!ok) { ReleaseResources(); Log("No se pudieron crear las texturas de efectos (%ux%u)", g_w, g_h); }
    g_resOk = ok;
    return ok;
}

