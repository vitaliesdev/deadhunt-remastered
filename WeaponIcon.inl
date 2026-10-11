// DeadhuntFX - side view of the weapon in hand (base of the weapon icon for the new HUD).
// The weapon is drawn after the game's depth clear with the fixed-function pipeline (world*view =
// camera space, so only the projection changes) or a skinned shader (VS_CAM: c(base).. = projection).
// Each weapon draw is issued a second time into an off-screen target with an orthographic camera
// looking at the weapon's right side (camera +z = icon right, +y = icon up).
// Debug capture: create DeadhuntFX_arma.txt in the game folder -> DeadhuntFX_arma.bmp
// (the weapon on magenta, cropped). Included from DeadhuntFX.cpp after ShadowMap.inl.

static int  g_wiCapture;          // 0 off, 1 measure (main part), 2 render
static bool g_wiArmed;
static M4    g_wiMain, g_wiMainInv;  // main weapon part: model -> camera space, and back
static float g_wiBoxMin[3], g_wiBoxMax[3];
static UINT  g_wiBest;

static bool Invert4(const M4& a, M4& r)
{
    float m[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) m[i][j] = j < 4 ? a.m[i][j] : (j - 4 == i ? 1.0f : 0.0f);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int i = c + 1; i < 4; i++) if (fabsf(m[i][c]) > fabsf(m[p][c])) p = i;
        if (fabsf(m[p][c]) < 1e-12f) return false;
        if (p != c) for (int j = 0; j < 8; j++) { float t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        float k = 1 / m[c][c];
        for (int j = 0; j < 8; j++) m[c][j] *= k;
        for (int i = 0; i < 4; i++) {
            if (i == c) continue;
            float f = m[i][c];
            for (int j = 0; j < 8; j++) m[i][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r.m[i][j] = m[i][j + 4];
    return true;
}

// Hands/glove textures of the first-person arms (not part of the weapon).
static bool IsArmTexture(IDirect3DDevice9* d)
{
    IDirect3DBaseTexture9* t = nullptr;
    if (FAILED(d->GetTexture(0, &t)) || !t) return false;
    D3DSURFACE_DESC td = {};
    if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td);
    t->Release();
    return (td.Width == 128 && td.Height == 128) || (td.Width == 256 && td.Height == 64) ||
           (S.handsW && td.Width == S.handsW && td.Height == S.handsH);
}
static IDirect3DSurface9 *g_wiRT, *g_wiDS;
static int g_wiDraws;
static const UINT WI_W = 1024, WI_H = 512;

static void WeaponIconRequestCheck()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX_arma.txt", g_dir);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    _wremove(path);
    g_wiArmed = true;
}

// Called after each weapon-phase draw.
static void WeaponIconDraw(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt, INT b, UINT mn, UINT nv, UINT si, UINT n)
{
    if (!g_wiCapture || g_inFX) return;
    IDirect3DVertexShader9* vs = nullptr;
    d->GetVertexShader(&vs);
    const VSInfo* info = vs ? FindVS(vs) : nullptr;
    if (vs) vs->Release();
    if (!g_depthDone || g_fxDone || Is2D(d)) return;
    if (vs || IsArmTexture(d)) return;                  // the weapon parts use the fixed-function pipeline
    if (g_wiCapture == 1) {                              // measure: the biggest part defines the weapon axes
        if (n <= g_wiBest || !mn && !nv) return;
        IDirect3DVertexBuffer9* vb = nullptr;
        UINT off = 0, stride = 0;
        d->GetStreamSource(0, &vb, &off, &stride);
        void* p = nullptr;
        if (vb && stride >= 12 && b >= 0 &&
            SUCCEEDED(vb->Lock(off + ((UINT)b + mn) * stride, nv * stride, &p, D3DLOCK_READONLY)) && p) {
            for (int k = 0; k < 3; k++) { g_wiBoxMin[k] = 1e30f; g_wiBoxMax[k] = -1e30f; }
            for (UINT i = 0; i < nv; i++) {
                const float* v = (const float*)((const BYTE*)p + (size_t)i * stride);
                for (int k = 0; k < 3; k++) { g_wiBoxMin[k] = fminf(g_wiBoxMin[k], v[k]); g_wiBoxMax[k] = fmaxf(g_wiBoxMax[k], v[k]); }
            }
            vb->Unlock();
            g_wiMain = Mul(ToM4(g_curWorld), ToM4(g_lastView));
            g_wiBest = n;
        }
        if (vb) vb->Release();
        return;
    }

    if (g_wiCapture != 2) return;
    if (!g_wiRT) {
        if (FAILED(d->CreateRenderTarget(WI_W, WI_H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_wiRT, nullptr)) ||
            FAILED(d->CreateDepthStencilSurface(WI_W, WI_H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &g_wiDS, nullptr))) {
            Log("Icono del arma: no se pudo crear la imagen");
            g_wiCapture = 0;
            return;
        }
    }
    IDirect3DSurface9 *oldRT = nullptr, *oldDS = nullptr;
    D3DVIEWPORT9 oldVP;
    d->GetRenderTarget(0, &oldRT);
    d->GetDepthStencilSurface(&oldDS);
    d->GetViewport(&oldVP);
    d->SetRenderTarget(0, g_wiRT);
    d->SetDepthStencilSurface(g_wiDS);
    D3DVIEWPORT9 vp = { 0, 0, WI_W, WI_H, 0, 1 };
    d->SetViewport(&vp);
    if (g_wiDraws == 0) d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);

    // camera space -> main part model space -> side view: longest box axis = icon x, second = icon y
    float ext[3], ctr[3];
    for (int k = 0; k < 3; k++) { ext[k] = g_wiBoxMax[k] - g_wiBoxMin[k]; ctr[k] = (g_wiBoxMax[k] + g_wiBoxMin[k]) * 0.5f; }
    int ax = 0;
    for (int k = 1; k < 3; k++) if (ext[k] > ext[ax]) ax = k;
    int ay = ax == 0 ? 1 : 0;
    for (int k = 0; k < 3; k++) if (k != ax && ext[k] > ext[ay]) ay = k;
    int az = 3 - ax - ay;
    float span = fmaxf(ext[ax], ext[ay] * 2) * 1.6f;      // room for parts outside the main one (magazine, stock)
    float hw = span, hh = span * 0.5f, dz = fmaxf(ext[az], span) * 4;
    M4 side = {};
    side.m[ax][0] = 1 / hw;  side.m[3][0] = -ctr[ax] / hw;
    side.m[ay][1] = 1 / hh;  side.m[3][1] = -ctr[ay] / hh;
    side.m[az][2] = 1 / dz;  side.m[3][2] = 0.5f - ctr[az] / dz;
    side.m[3][3] = 1;
    M4 m = Mul(g_wiMainInv, side);
    float c[256][4], saved[4][4];
    D3DMATRIX oldProj = g_curProjM;
    if (vs) {
        d->GetVertexShaderConstantF(0, &c[0][0], 256);
        memcpy(saved, c[info->base], sizeof(saved));
        WriteMatrixConsts(c, info->base, m);
        d->SetVertexShaderConstantF(info->base, &c[info->base][0], 4);
    } else {
        D3DMATRIX p;
        memcpy(p.m, m.m, sizeof(p.m));
        oSetTransform(d, D3DTS_PROJECTION, &p);     // world*view = camera space already
    }
    DWORD cull, stencil;
    d->GetRenderState(D3DRS_CULLMODE, &cull);
    d->GetRenderState(D3DRS_STENCILENABLE, &stencil);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    oDIP(d, pt, b, mn, nv, si, n);
    d->SetRenderState(D3DRS_CULLMODE, cull);
    d->SetRenderState(D3DRS_STENCILENABLE, stencil);
    if (vs) d->SetVertexShaderConstantF(info->base, &saved[0][0], 4);
    else oSetTransform(d, D3DTS_PROJECTION, &oldProj);
    d->SetRenderTarget(0, oldRT);
    d->SetDepthStencilSurface(oldDS);
    d->SetViewport(&oldVP);
    if (oldRT) oldRT->Release();
    if (oldDS) oldDS->Release();
    g_wiDraws++;
}

// Called from Present: arm the capture for the next frame, save the finished one.
static void WeaponIconFrame(IDirect3DDevice9* d)
{
    WeaponIconRequestCheck();
    if (g_wiCapture == 1) {
        if (g_wiBest && Invert4(g_wiMain, g_wiMainInv)) {
            Log("Icono del arma: pieza principal %u prims, caja %.1f %.1f %.1f", g_wiBest,
                g_wiBoxMax[0] - g_wiBoxMin[0], g_wiBoxMax[1] - g_wiBoxMin[1], g_wiBoxMax[2] - g_wiBoxMin[2]);
            g_wiCapture = 2;
        } else {
            Log("Icono del arma: no se encontro la pieza principal");
            g_wiCapture = 0;
        }
        return;
    }
    if (g_wiCapture == 2) {
        g_wiCapture = 0;
        IDirect3DSurface9* sys = nullptr;
        if (g_wiDraws && g_wiRT &&
            SUCCEEDED(d->CreateOffscreenPlainSurface(WI_W, WI_H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
            SUCCEEDED(d->GetRenderTargetData(g_wiRT, sys))) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                int x0 = WI_W, y0 = WI_H, x1 = -1, y1 = -1;
                for (UINT y = 0; y < WI_H; y++) {
                    const DWORD* row = (const DWORD*)((const BYTE*)lr.pBits + (size_t)y * lr.Pitch);
                    for (UINT x = 0; x < WI_W; x++)
                        if ((row[x] & 0xFFFFFF) != 0xFF00FF) {
                            x0 = min(x0, (int)x); x1 = max(x1, (int)x);
                            y0 = min(y0, (int)y); y1 = max(y1, (int)y);
                        }
                }
                if (x1 >= x0) {
                    x0 = max(0, x0 - 4); y0 = max(0, y0 - 4);
                    x1 = min((int)WI_W - 1, x1 + 4); y1 = min((int)WI_H - 1, y1 + 4);
                    UINT w = x1 - x0 + 1, h = y1 - y0 + 1, rowB = (w * 3 + 3) & ~3u;
                    wchar_t path[MAX_PATH];
                    swprintf_s(path, L"%sDeadhuntFX_arma.bmp", g_dir);
                    FILE* f = nullptr;
                    if (_wfopen_s(&f, path, L"wb") == 0 && f) {
                        BITMAPFILEHEADER fh = { 0x4D42, (DWORD)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + rowB * h), 0, 0,
                                                (DWORD)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) };
                        BITMAPINFOHEADER ih = { sizeof(BITMAPINFOHEADER), (LONG)w, (LONG)h, 1, 24, BI_RGB, rowB * h, 0, 0, 0, 0 };
                        fwrite(&fh, sizeof(fh), 1, f);
                        fwrite(&ih, sizeof(ih), 1, f);
                        std::vector<unsigned char> line(rowB, 0);
                        for (int y = y1; y >= y0; y--) {
                            const BYTE* px = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch + (size_t)x0 * 4;
                            for (UINT x = 0; x < w; x++) memcpy(&line[x * 3], px + x * 4, 3);
                            fwrite(line.data(), rowB, 1, f);
                        }
                        fclose(f);
                    }
                    Log("Icono del arma: %d dibujos, recorte %d,%d - %d,%d guardado", g_wiDraws, x0, y0, x1, y1);
                } else {
                    Log("Icono del arma: %d dibujos pero la imagen esta vacia (fuera del encuadre)", g_wiDraws);
                }
                sys->UnlockRect();
            }
        } else {
            Log("Icono del arma: ningun dibujo del arma en el fotograma");
        }
        if (sys) sys->Release();
        if (g_wiRT) { g_wiRT->Release(); g_wiRT = nullptr; }
        if (g_wiDS) { g_wiDS->Release(); g_wiDS = nullptr; }
    }
    if (g_wiArmed) { g_wiArmed = false; g_wiCapture = 1; g_wiDraws = 0; g_wiBest = 0; }
}
