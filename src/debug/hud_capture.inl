// DeadhuntFX - HUD/menu diagnostics: F11 captures one frame of draw calls into
// DeadhuntFX_hud.log (phase, draw type, screen rectangle, texture). Nothing visible changes.
// Included from main.cpp right before the draw hooks.

static bool g_hudCapture, g_hudCaptureNext;
static void WorldCaptureEnd();
static FILE* g_hudLog;
static int g_hudLines;

static void HudTransform(const float* p, float out[2])
{
    // world * view * projection -> screen pixels
    const D3DMATRIX* ms[3] = { &g_curWorld, &g_lastView, &g_curProjM };
    float v[4] = { p[0], p[1], p[2], 1 };
    for (const D3DMATRIX* m : ms) {
        float r[4];
        for (int j = 0; j < 4; j++) r[j] = v[0] * m->m[0][j] + v[1] * m->m[1][j] + v[2] * m->m[2][j] + v[3] * m->m[3][j];
        memcpy(v, r, sizeof(v));
    }
    float w = fabsf(v[3]) > 1e-6f ? v[3] : 1;
    out[0] = (v[0] / w * 0.5f + 0.5f) * g_w;
    out[1] = (0.5f - v[1] / w * 0.5f) * g_h;
}

// Screen bounding box of the vertices (positions are the first 3 or 4 floats).
static void HudBBox(const unsigned char* verts, UINT count, UINT stride, bool rhw, float bb[4])
{
    bb[0] = bb[1] = 1e9f; bb[2] = bb[3] = -1e9f;
    for (UINT i = 0; i < count; i++) {
        const float* p = (const float*)(verts + (size_t)i * stride);
        float s[2];
        if (rhw) { s[0] = p[0]; s[1] = p[1]; }
        else HudTransform(p, s);
        bb[0] = fminf(bb[0], s[0]); bb[1] = fminf(bb[1], s[1]);
        bb[2] = fmaxf(bb[2], s[0]); bb[3] = fmaxf(bb[3], s[1]);
    }
}

static void HudLogDraw(IDirect3DDevice9* d, const char* api, D3DPRIMITIVETYPE pt, UINT prims,
                       const void* upVerts, UINT upStride, UINT firstVert, UINT numVerts)
{
    if (!g_hudCapture || g_inFX || !g_hudLog || g_hudLines > 1500) return;
    DWORD fvf = 0;
    d->GetFVF(&fvf);
    IDirect3DVertexShader9* vs = nullptr;
    d->GetVertexShader(&vs);
    bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    bool ortho = fabsf(g_curProjM._34) < 0.5f;

    // vertex positions: user pointer, or read back from stream 0
    float bb[4] = { 0, 0, 0, 0 };
    bool haveBB = false;
    if (!vs && (rhw || (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZ)) {
        if (upVerts) {
            HudBBox((const unsigned char*)upVerts + (size_t)firstVert * upStride, numVerts, upStride, rhw, bb);
            haveBB = true;
        } else {
            IDirect3DVertexBuffer9* vb = nullptr;
            UINT off = 0, stride = 0;
            d->GetStreamSource(0, &vb, &off, &stride);
            void* data = nullptr;
            if (vb && stride && SUCCEEDED(vb->Lock(off + firstVert * stride, numVerts * stride, &data, D3DLOCK_READONLY))) {
                HudBBox((const unsigned char*)data, numVerts, stride, rhw, bb);
                if (!rhw && prims > 40 && IniF(L"Debug", L"DumpVerts", 0) != 0) {      // panel meshes: vertex list
                    fprintf(g_hudLog, "  vertices (%u, stride %u):", numVerts, stride);
                    for (UINT i = 0; i < numVerts; i++) {
                        const float* p = (const float*)((const unsigned char*)data + (size_t)i * stride);
                        float sc[2];
                        HudTransform(p, sc);
                        fprintf(g_hudLog, " %.0f,%.0f", sc[0], sc[1]);
                    }
                    fputc('\n', g_hudLog);
                }
                vb->Unlock();
                haveBB = true;
            }
            if (vb) vb->Release();
        }
    }

    char tex[64] = "sin textura";
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    if (t) {
        if (t->GetType() == D3DRTYPE_TEXTURE) {
            D3DSURFACE_DESC desc;
            ((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc);
            sprintf_s(tex, "tex %p %ux%u fmt %d", (void*)t, desc.Width, desc.Height, (int)desc.Format);
        }
        t->Release();
    }
    DWORD ab = 0, zen = 0;
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    d->GetRenderState(D3DRS_ZENABLE, &zen);
    fprintf(g_hudLog, "%-6s %-5s %-7s prims %4u | %s | %s%s%s | rect %s",
            g_fxDone ? "HUD" : "mundo", api, vs ? "shader" : rhw ? "pantal" : ortho ? "orto2D" : "3D", prims, tex,
            ab ? "mezcla " : "", zen ? "z " : "", vs ? "" : "", haveBB ? "" : "?");
    if (haveBB) fprintf(g_hudLog, "%.0f,%.0f - %.0f,%.0f", bb[0], bb[1], bb[2], bb[3]);
    fprintf(g_hudLog, "\n");
    g_hudLines++;
    if (vs) vs->Release();
}

static UINT VertsForPrims(D3DPRIMITIVETYPE pt, UINT prims)
{
    return pt == D3DPT_TRIANGLELIST ? prims * 3 : pt == D3DPT_LINELIST ? prims * 2 : prims + 2;
}

// Saves the finished frame (what is on screen) as a BMP next to the log.
static bool g_shotPending;

static void SaveScreenshot(IDirect3DDevice9* d)
{
    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);
    if (desc.MultiSampleType == D3DMULTISAMPLE_NONE &&
        SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(d->GetRenderTargetData(bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%sDeadhuntFX_captura_%02d%02d%02d.bmp", g_dir, st.wHour, st.wMinute, st.wSecond);
            FILE* f = nullptr;
            if (_wfopen_s(&f, path, L"wb") == 0 && f) {
                UINT w = desc.Width, h = desc.Height, row = (w * 3 + 3) & ~3u;
                BITMAPFILEHEADER fh = { 0x4D42, (DWORD)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + row * h), 0, 0,
                                        (DWORD)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) };
                BITMAPINFOHEADER ih = { sizeof(BITMAPINFOHEADER), (LONG)w, (LONG)h, 1, 24, BI_RGB, row * h, 0, 0, 0, 0 };
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                unsigned char* line = (unsigned char*)calloc(row, 1);
                for (int y = (int)h - 1; y >= 0 && line; y--) {   // BMP rows go bottom-up
                    const unsigned char* px = (const unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
                    for (UINT x = 0; x < w; x++) {                // X8R8G8B8 / A8R8G8B8 = B,G,R,A in memory
                        line[x * 3 + 0] = px[x * 4 + 0];
                        line[x * 3 + 1] = px[x * 4 + 1];
                        line[x * 3 + 2] = px[x * 4 + 2];
                    }
                    fwrite(line, row, 1, f);
                }
                free(line);
                fclose(f);
                Log("Imagen guardada: %ls", path);
            }
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release();
    bb->Release();
}

// Remote control for automated testing:
//  - DeadhuntFX_cmd.txt containing "shot" = same as pressing F11 (file is deleted)
//  - DeadhuntFX_state.txt is rewritten every few frames: game cursor position, menu or game
static float g_cursorX = -1, g_cursorY = -1;
static int g_hudDraws, g_hudMenuDraws, g_hudRej[16];   // HUD layout stats (hud_layout.inl)
static bool g_frameHad3D;

static void NoteCursor(IDirect3DDevice9* d, UINT prims, const void* verts, UINT stride)
{
    if (prims != 2 || !verts || !Is2D(d)) return;
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    if (!t) return;
    D3DSURFACE_DESC desc;
    bool cur = t->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc)) &&
               desc.Width == 64 && desc.Height == 64;
    t->Release();
    if (!cur) return;
    float bb[4];
    HudBBox((const unsigned char*)verts, 4, stride, true, bb);
    if (fabsf((bb[2] - bb[0]) - (bb[3] - bb[1])) < 2 && bb[2] - bb[0] < 130) { g_cursorX = bb[0]; g_cursorY = bb[1]; }
}

static bool RemoteShotRequested()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX_cmd.txt", g_dir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r") != 0 || !f) return false;
    char buf[64] = "";
    fgets(buf, sizeof(buf), f);
    fclose(f);
    _wremove(path);
    return strncmp(buf, "shot", 4) == 0;
}

static void WriteState(int frame)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX_state.txt", g_dir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") != 0 || !f) return;
    fprintf(f, "frame %d\ncursor %.0f %.0f\nscreen %s\n", frame, g_cursorX, g_cursorY,
            g_frameHad3D ? "juego" : "menu");
    fprintf(f, "hud %d menu %d rej", g_hudDraws, g_hudMenuDraws);
    for (int i = 1; i < 16; i++) fprintf(f, " %d", g_hudRej[i]);
    fprintf(f, "\n");
    fclose(f);
}

// Called once per frame from Present.
static void HudCaptureFrame()
{
    static bool was;
    static int frame;
    if (++frame % 5 == 0) WriteState(frame);
    bool remote = (frame % 5 == 0) && RemoteShotRequested();
    if (g_hudCapture) {
        if (g_hudLog) { fprintf(g_hudLog, "---- fin del fotograma (%d dibujos)\n\n", g_hudLines); fclose(g_hudLog); }
        g_hudLog = nullptr;
        g_hudCapture = false;
        WorldCaptureEnd();
        Log("Captura de HUD guardada en DeadhuntFX_hud.log");
    }
    if (KeyPressed(VK_F11, was) || remote) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%sDeadhuntFX_hud.log", g_dir);
        if (_wfopen_s(&g_hudLog, path, L"a") == 0 && g_hudLog) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(g_hudLog, "==== Captura %02d:%02d:%02d, pantalla %ux%u\n", st.wHour, st.wMinute, st.wSecond, g_w, g_h);
            g_hudCapture = true;
            g_hudLines = 0;
            g_shotPending = true;
        }
    }
}
