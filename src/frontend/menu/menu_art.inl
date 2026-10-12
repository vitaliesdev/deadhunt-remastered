// DeadhuntFX - remastered main menu art.
//
// The main menu draws GUI_MAINBACKPLANE (an 800x600 JPEG stretched over the whole screen) and the
// DEADHUNT logo (256x64, stretched too). When DeadhuntFX\menu_fondo.bin / menu_logo.bin exist
// (raw 'DHI1' w h + BGRA: tools/generators/menu_art.py makes the background from the upscaled art,
// tools/generators/menu_logo.py draws the logo with Oswald), those draws are
// replaced: the background keeps its aspect at full height, right-aligned, and only its left 14 %
// (haze, no figures) is stretched over the rest of a wide screen; the logo keeps its aspect.
// Included from main.cpp after menu_skin.inl.

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

static IDirect3DTexture9 *g_menuBg, *g_menuLogo;
static float g_menuBgAspect = 4.0f / 3.0f, g_menuLogoAspect = 4.0f;
static bool g_menuArtTried;
static void MenuArtEnsure(IDirect3DDevice9* d);

static IDirect3DTexture9* LoadRawImage(IDirect3DDevice9* d, const wchar_t* name, float* aspect)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX\\%s", g_dir, name);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return nullptr;
    char magic[4];
    UINT wh[2];
    IDirect3DTexture9* t = nullptr;
    if (fread(magic, 4, 1, f) == 1 && !memcmp(magic, "DHI1", 4) && fread(wh, 8, 1, f) == 1 &&
        wh[0] && wh[1] && wh[0] <= 8192 && wh[1] <= 8192) {
        g_inFX = true;
        HRESULT hr = d->CreateTexture(wh[0], wh[1], 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
        g_inFX = false;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(hr) && t && SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
            bool ok = true;
            for (UINT y = 0; y < wh[1] && ok; y++) ok = fread((char*)lr.pBits + (size_t)y * lr.Pitch, wh[0] * 4, 1, f) == 1;
            t->UnlockRect(0);
            if (ok) {
                FillMips(t);
                *aspect = (float)wh[0] / (float)wh[1];
                Log("Menu: %ls cargado (%ux%u)", name, wh[0], wh[1]);
            } else { t->Release(); t = nullptr; }
        } else if (t) { t->Release(); t = nullptr; }
    }
    fclose(f);
    return t;
}

// JPG / PNG through the Windows Imaging Component (comes with Windows), into a mipmapped texture.
// The decode runs on its own short-lived thread: the game's render thread has its own COM apartment
// and FPU settings, and WIC failed there.
struct WicJob { const wchar_t* path; UINT w, h; std::vector<BYTE> px; HRESULT hr; int step; };

static DWORD WINAPI WicThread(void* arg)
{
    WicJob* j = (WicJob*)arg;
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* fac = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICFormatConverter* cv = nullptr;
    j->step = 1; j->hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (SUCCEEDED(j->hr)) { j->step = 2; j->hr = fac->CreateDecoderFromFilename(j->path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec); }
    if (SUCCEEDED(j->hr)) { j->step = 3; j->hr = dec->GetFrame(0, &fr); }
    if (SUCCEEDED(j->hr)) { j->step = 4; j->hr = fac->CreateFormatConverter(&cv); }
    if (SUCCEEDED(j->hr)) { j->step = 5; j->hr = cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom); }
    if (SUCCEEDED(j->hr)) { j->step = 6; j->hr = cv->GetSize(&j->w, &j->h); }
    if (SUCCEEDED(j->hr) && j->w && j->h && j->w <= 8192 && j->h <= 8192) {
        j->step = 7;
        j->px.resize((size_t)j->w * j->h * 4);
        j->hr = cv->CopyPixels(nullptr, j->w * 4, (UINT)j->px.size(), j->px.data());
        if (SUCCEEDED(j->hr)) j->step = 0;
    }
    if (cv) cv->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (fac) fac->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

static IDirect3DTexture9* LoadWicImage(IDirect3DDevice9* d, const wchar_t* name, float* aspect)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX\\%s", g_dir, name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { Log("Menu: no existe %ls", path); return nullptr; }
    WicJob job = { path, 0, 0, {}, E_FAIL, -1 };
    HANDLE th = CreateThread(nullptr, 0, WicThread, &job, 0, nullptr);
    if (th) { WaitForSingleObject(th, 10000); CloseHandle(th); }
    if (job.step != 0) { Log("Menu: no se pudo leer %ls (paso %d, 0x%08X)", name, job.step, (unsigned)job.hr); return nullptr; }
    IDirect3DTexture9* t = nullptr;
    g_inFX = true;
    HRESULT hr = d->CreateTexture(job.w, job.h, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    g_inFX = false;
    D3DLOCKED_RECT lr;
    if (FAILED(hr) || !t || FAILED(t->LockRect(0, &lr, nullptr, 0))) {
        if (t) t->Release();
        Log("Menu: textura para %ls no creada (0x%08X)", name, (unsigned)hr);
        return nullptr;
    }
    for (UINT y = 0; y < job.h; y++) memcpy((BYTE*)lr.pBits + (size_t)y * lr.Pitch, &job.px[(size_t)y * job.w * 4], job.w * 4);
    t->UnlockRect(0);
    FillMips(t);
    *aspect = (float)job.w / (float)job.h;
    Log("Menu: %ls cargado (%ux%u)", name, job.w, job.h);
    return t;
}

struct MenuVtx { float x, y, z, rhw; DWORD c; float u, v; };

static void MenuQuad(IDirect3DDevice9* d, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                     DWORD c = 0xFFFFFFFF)
{
    MenuVtx q[4] = { { x0 - .5f, y0 - .5f, 0, 1, c, u0, v0 }, { x1 - .5f, y0 - .5f, 0, 1, c, u1, v0 },
                     { x0 - .5f, y1 - .5f, 0, 1, c, u0, v1 }, { x1 - .5f, y1 - .5f, 0, 1, c, u1, v1 } };
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(MenuVtx));
}

// Draws the replacement with its own states and puts the game's back.
static void MenuArtDrawTex(IDirect3DDevice9* d, IDirect3DTexture9* tex, const float* bb, bool bg, float darken = 0)
{
    static const D3DRENDERSTATETYPE rs[] = { D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE,
                                             D3DRS_ZENABLE, D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_LIGHTING };
    static const DWORD rv[] = { TRUE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE };
    const int nrs = sizeof(rs) / sizeof(rs[0]);
    DWORD saveRs[16], fvf, ts[7], ss[5];
    IDirect3DBaseTexture9* old = nullptr;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    for (int i = 0; i < nrs; i++) d->GetRenderState(rs[i], &saveRs[i]);
    d->GetFVF(&fvf);
    d->GetTexture(0, &old);
    d->GetVertexShader(&vs);
    d->GetPixelShader(&ps);
    static const D3DTEXTURESTAGESTATETYPE tst[6] = { D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1,
                                                    D3DTSS_ALPHAARG2 };
    for (int i = 0; i < 6; i++) d->GetTextureStageState(0, tst[i], &ts[i]);
    d->GetTextureStageState(1, D3DTSS_COLOROP, &ts[6]);
    static const D3DSAMPLERSTATETYPE sst[5] = { D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV };
    for (int i = 0; i < 5; i++) d->GetSamplerState(0, sst[i], &ss[i]);

    for (int i = 0; i < nrs; i++) d->SetRenderState(rs[i], rv[i]);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    d->SetTexture(0, tex);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    float W = (float)g_w, H = (float)g_h;
    g_inFX = true;
    if (bg) {
        float iw = H * g_menuBgAspect;
        if (iw >= W) {                                   // narrower than the art: crop its left side
            float u0 = 1 - W / iw;
            MenuQuad(d, 0, 0, W, H, u0, 0, 1, 1);
        } else {
            const float split = 0.14f;                   // left part of the art: haze only
            float xs = W - iw * (1 - split);
            MenuQuad(d, 0, 0, xs, H, 0, 0, split, 1);
            MenuQuad(d, xs, 0, W, H, split, 0, 1, 1);
        }
        if (darken > 0) MenuQuad(d, 0, 0, W, H, 0, 0, 1, 1, (DWORD)(darken * 255 + .5f) << 24);   // black, alpha = darken
    } else {
        // our logo: DEADHUNT is the top ~70 % of the image, REMASTERED below; the title takes the
        // original's place (a bit larger) and the red line hangs under it
        float lh = (bb[3] - bb[1]) * 1.8f, lw = lh * g_menuLogoAspect;
        float top = (bb[1] + bb[3]) * .5f - lh * 0.36f;
        MenuQuad(d, bb[0], top, bb[0] + lw, top + lh, 0, 0, 1, 1);
    }
    g_inFX = false;

    for (int i = 0; i < nrs; i++) d->SetRenderState(rs[i], saveRs[i]);
    d->SetFVF(fvf);
    d->SetTexture(0, old);
    if (old) old->Release();
    d->SetVertexShader(vs);
    if (vs) vs->Release();
    d->SetPixelShader(ps);
    if (ps) ps->Release();
    for (int i = 0; i < 6; i++) d->SetTextureStageState(0, tst[i], ts[i]);
    d->SetTextureStageState(1, D3DTSS_COLOROP, ts[6]);
    for (int i = 0; i < 5; i++) d->SetSamplerState(0, sst[i], ss[i]);
}

// Draw hook (indexed 2D-camera draws in the menus): true when the game's draw was replaced.
static bool MenuArtDraw(IDirect3DDevice9* d, INT b, UINT mn, UINT nv, bool* isBg)
{
    *isBg = false;
    if (!S.enabled || g_inFX || g_drawsSince3D > 0 || g_inGameHud || Is2D(d) || fabsf(g_curProjM._34) > 0.5f) return false;
    MenuArtEnsure(d);
    if (!g_menuBg && !g_menuLogo) return false;
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    D3DSURFACE_DESC td = {};
    if (t) {
        if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td);
        t->Release();
    }
    bool bgTex = td.Width == 1024 && td.Height == 1024, logoTex = td.Width == 256 && td.Height == 64;
    if (!(bgTex && g_menuBg) && !(logoTex && g_menuLogo)) return false;
    float bb[4];
    if (!DrawBBox(d, b, mn, nv, bb)) return false;
    float W = (float)g_w, H = (float)g_h;
    if (bgTex && bb[2] - bb[0] >= W * 0.98f && bb[3] - bb[1] >= H * 0.98f) {
        MenuArtDrawTex(d, g_menuBg, bb, true);
        *isBg = true;
        g_skinFrameBg = true;
        return true;
    }
    if (logoTex && bb[0] < W * 0.3f && bb[3] < H * 0.4f && bb[2] - bb[0] > W * 0.15f) {
        MenuArtDrawTex(d, g_menuLogo, bb, false);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- screens without a background
// The game shows a white window while it loads at start-up, and plain black loading screens. Both get
// the menu art: once right after the device is created, and on every frame that draws no 3D and no
// full-screen background (judged from the previous frame), behind whatever the game draws.

static bool g_artPending, g_artDone;

static void MenuArtEnsure(IDirect3DDevice9* d)
{
    if (g_menuArtTried) return;
    g_menuArtTried = true;
    Log("Menu: cargando el arte del menu");
    g_menuBg = LoadWicImage(d, L"menu_fondo.jpg", &g_menuBgAspect);
    if (!g_menuBg) g_menuBg = LoadRawImage(d, L"menu_fondo.bin", &g_menuBgAspect);
    g_menuLogo = LoadWicImage(d, L"menu_logo.png", &g_menuLogoAspect);
    if (!g_menuLogo) g_menuLogo = LoadRawImage(d, L"menu_logo.bin", &g_menuLogoAspect);
}

static void MenuArtSplash(IDirect3DDevice9* d)
{
    MenuArtEnsure(d);
    if (!g_menuBg) return;
    float bb[4] = { 0, 0, (float)g_w, (float)g_h };
    MenuArtDrawTex(d, g_menuBg, bb, true, 0.35f);
    g_artDone = true;
}

// From BeforeDraw: the first draw of a frame that needs the art.
static void MenuArtBeforeDraw(IDirect3DDevice9* d)
{
    if (g_artPending && !g_artDone && !g_inFX && S.enabled && g_drawsSince3D == 0) MenuArtSplash(d);
}

// From Present, before the menu texts are drawn: frames with no draw at all (loading texts only).
static void MenuArtFrame(IDirect3DDevice9* d, bool had3D)
{
    if (g_artPending && !g_artDone && !had3D && S.enabled) MenuArtSplash(d);
    g_artPending = !had3D && !g_skinFrameBg && g_w > 0;
    g_artDone = false;
    g_skinFrameBg = false;
}

// Right after CreateDevice: one frame with the art, so the start-up wait is not a white window.
static void MenuArtStartup(IDirect3DDevice9* d, HWND wnd, const D3DPRESENT_PARAMETERS* pp)
{
    if (!S.enabled || !oPresent) return;
    UINT w = pp->BackBufferWidth, h = pp->BackBufferHeight;
    RECT rc;
    if ((!w || !h) && GetClientRect(wnd, &rc)) { w = rc.right; h = rc.bottom; }
    if (!w || !h) return;
    UINT ow = g_w, oh = g_h;
    g_w = w; g_h = h;
    if (SUCCEEDED(d->BeginScene())) {
        d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0);
        MenuArtSplash(d);
        d->EndScene();
        g_inFX = true;
        oPresent(d, nullptr, nullptr, nullptr, nullptr);
        g_inFX = false;
    }
    g_artDone = false;
    g_w = ow; g_h = oh;
}
