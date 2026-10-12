// Draw classification: 2D/HUD detection, hands tagging, before/after draw. (part of the unity build, see src/main.cpp)
typedef HRESULT(STDMETHODCALLTYPE* PFN_DP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DPUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIPUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*,
                                              D3DFORMAT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CTex)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                             IDirect3DTexture9**, HANDLE*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CVB)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL,
                                            IDirect3DVertexBuffer9**, HANDLE*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CIB)(IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                            IDirect3DIndexBuffer9**, HANDLE*);
static PFN_DP oDP;
static PFN_DIP oDIP;
static PFN_DPUP oDPUP;
static PFN_DIPUP oDIPUP;
static PFN_CTex oCTex;
static PFN_CVB oCVB;
static PFN_CIB oCIB;

// Screen-space (pre-transformed) vertices = 2D. The first 2D draw after the world is the HUD.
static bool Is2D(IDirect3DDevice9* d)
{
    DWORD fvf = 0;
    d->GetFVF(&fvf);
    return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

// Weapon phase = after the game's depth clear, before the HUD.
struct SeenTex { IDirect3DBaseTexture9* t; };
static SeenTex g_seenTex[64];
static DWORD g_savedStencil[7];
static bool g_tagging;

static bool IsHandsTexture(IDirect3DDevice9* d)
{
    IDirect3DBaseTexture9* t = nullptr;
    if (FAILED(d->GetTexture(0, &t)) || !t) return false;
    bool hands = false;
    if (t->GetType() == D3DRTYPE_TEXTURE) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc))) {
            hands = S.handsW && desc.Width == S.handsW && desc.Height == S.handsH;
            bool known = false;
            for (auto& e : g_seenTex) {
                if (e.t == t) { known = true; break; }
                if (!e.t) {
                    e.t = t;
                    Log("Textura del arma/mano: %ux%u formato %d niveles %u%s", desc.Width, desc.Height,
                        (int)desc.Format, t->GetLevelCount(), hands ? "  <- mano" : "");
                    known = true;
                    break;
                }
            }
        }
    }
    t->Release();
    return hands;
}

static void TagBegin(IDirect3DDevice9* d)
{
    static const D3DRENDERSTATETYPE rs[7] = { D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
                                              D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL,
                                              D3DRS_STENCILZFAIL };
    for (int i = 0; i < 7; i++) d->GetRenderState(rs[i], &g_savedStencil[i]);
    bool hands = IsHandsTexture(d);
    d->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    d->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    d->SetRenderState(D3DRS_STENCILREF, hands ? 2 : 1);
    d->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
    d->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    d->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    d->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    g_tagging = true;
}

static void AfterDraw(IDirect3DDevice9* d)
{
    if (!g_tagging) return;
    static const D3DRENDERSTATETYPE rs[7] = { D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
                                              D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL,
                                              D3DRS_STENCILZFAIL };
    for (int i = 0; i < 7; i++) d->SetRenderState(rs[i], g_savedStencil[i]);
    g_tagging = false;
}

static void MenuArtBeforeDraw(IDirect3DDevice9* d);

static void BeforeDraw(IDirect3DDevice9* d)
{
    if (g_inFX) return;
    MenuArtBeforeDraw(d);
    if (g_depthDone && !g_fxDone && g_depthTex && S.enabled && S.wpn && !Is2D(d))
        TagBegin(d);
    if (Is2D(d)) {
        if (g_drawsSince3D > 0 && !g_fxDone && S.enabled) {
            g_inFX = true;
            RunFX(d, g_depthDone ? STAGE_POST : STAGE_ALL);
            g_inFX = false;
            g_fxDone = true;
            g_hudByFvf++;
        }
    } else if (!g_fxDone) {
        // world = perspective camera or the models' vertex shaders (menus use a 2D camera)
        IDirect3DVertexShader9* vs = nullptr;
        d->GetVertexShader(&vs);
        if (vs) vs->Release();
        if (vs || fabsf(g_curProjM._34) > 0.5f) g_drawsSince3D++;
    }
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_SetTransform)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
static PFN_SetTransform oSetTransform;

