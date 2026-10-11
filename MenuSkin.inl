// DeadhuntFX - menu skin: the game's menus redrawn in the new style ([HUD] MenuSkin=1).
//
//  - Texts: every GUI text goes through the engine renderer (0x494930). Outside the in-game HUD
//    pass it is replaced by Oxanium from our atlas, same rectangle and alignment, with
//    the old palette (light blue labels, yellow values, grey buttons) mapped to bone / red.
//    Texts using glyphs our font lacks (the game's lock and tick symbols) stay the game's.
//  - Panels: the blue frame meshes (2D camera, 64x64 tile textures) become a smoked-glass panel
//    with a fine border, corner brackets and a red top line; boxes inside a panel get a plain
//    darker fill. Sliders become a thin track with a red fill and a bone diamond.
//  - Full-screen backgrounds get a dark gradient behind the left-hand text.
// Panels are drawn immediately (to keep the order with the game's other draws); texts are
// collected and drawn last, right before Present. Included after NewHud.inl.

static const DWORD C_MUTED = 0x8A8680, C_BLOOD = 0xE2361F;
static float g_skinPanels[16][4];
static int   g_skinPanelN;
static float g_skinBar[4];
static bool  g_skinBarSet;
static bool  g_skinFrameBg;          // a full-screen background was drawn this frame (MenuArt.inl)
static float g_skinThumbW;          // width of the game's slider thumb (last seen)

// Message boxes (GUI_StdMsg: quit, back to menu, delete player) are as wide as most of the screen on
// 16:9. Their panel is drawn to fit the texts inside (question, SI / NO) instead; the buttons stay
// where the game expects the clicks. The text box comes from the previous frame.
static float g_dlgPanel[4], g_dlgText[4], g_dlgPrev[4];
static bool g_dlgHavePanel, g_dlgHaveText, g_dlgPrevOk;

static bool IsStdMsg()
{
    DWORD m = *(DWORD*)0x6b71ec;
    if (!m || IsBadReadPtr((void*)(m + 0x80), 4)) return false;
    DWORD w = *(DWORD*)(m + 0x80);
    return w && !IsBadReadPtr((void*)w, 4) && *(DWORD*)w == 0x67801c;
}

static void DialogTextBox(float x0, float y0, float x1, float y1)
{
    if (!g_dlgHavePanel) return;
    float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
    if (cx < g_dlgPanel[0] || cx > g_dlgPanel[2] || cy < g_dlgPanel[1] || cy > g_dlgPanel[3]) return;
    if (!g_dlgHaveText) { g_dlgText[0] = x0; g_dlgText[1] = y0; g_dlgText[2] = x1; g_dlgText[3] = y1; g_dlgHaveText = true; return; }
    g_dlgText[0] = fminf(g_dlgText[0], x0); g_dlgText[1] = fminf(g_dlgText[1], y0);
    g_dlgText[2] = fmaxf(g_dlgText[2], x1); g_dlgText[3] = fmaxf(g_dlgText[3], y1);
}

static float g_creditRect[4];
static DWORD g_creditSeen;

// Once per frame: a click on the credit line opens the Steam profile.
static void CreditClickFrame()
{
    static bool was;
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool press = down && !was;
    was = down;
    if (!press || GetTickCount() - g_creditSeen > 200) return;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return;
    if (g_cursorX < g_creditRect[0] || g_cursorX > g_creditRect[2] || g_cursorY < g_creditRect[1] || g_cursorY > g_creditRect[3]) return;
    ShellExecuteW(nullptr, L"open", L"https://steamcommunity.com/id/reptilemk/", nullptr, nullptr, SW_SHOWNORMAL);
    Log("Menu: abierto el perfil del autor");
}

static bool SkinOn() { return S.menuSkin && S.enabled && !g_inGameHud && !g_hudFailed; }

// old palette -> new
static DWORD SkinColor(DWORD c, bool* highlight)
{
    DWORD rgb = c & 0xFFFFFF;
    *highlight = false;
    switch (rgb) {
    case 0x93C3C4: return C_DIM;                       // labels (light blue)
    case 0xFBF2BD: *highlight = true; return C_BONE;    // values / selected tab (yellow)
    case 0xFFFFFF: *highlight = true; return C_BONE;    // hover
    case 0xFFFFAA: *highlight = true; return C_BONE;    // selected list row
    case 0xAAAA99: return C_MUTED;                      // buttons at rest
    case 0x666655: return C_MUTED;                      // list rows
    case 0x333322: return 0x4A433A;                     // disabled rows
    case 0xB1AFAF: return C_MUTED;                      // copyright
    }
    int lum = ((rgb >> 16) & 255) * 3 + ((rgb >> 8) & 255) * 6 + (rgb & 255);
    return lum > 2000 ? C_BONE : lum > 1200 ? C_DIM : C_MUTED;
}

static bool SkinCanDraw(const char* s)
{
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        if (!g_gBar[*p].ok) return false;
    return true;
}

// One GUI text in the new style (rect = the game's text rectangle, flags 1 = centred, 0x10 = right)
static bool SkinText(int size, float x1, float y1, float x2, float y2, DWORD col, const char* s, DWORD flags)
{
    if (!s || !*s || IsBadReadPtr(s, 1) || !SkinCanDraw(s)) return false;
    bool hi;
    DWORD rgb = SkinColor(col, &hi);
    float a = ((col >> 24) & 255) / 255.0f;
    float px = size * 0.52f, base = y1 + size * 0.66f;
    int align = (flags & 1) ? 1 : (flags & 0x10) ? 2 : 0;
    float x = align == 1 ? (x1 + x2) * .5f : align == 2 ? x2 : x1;
    bool caps = true;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) if (*p >= 'a' && *p <= 'z') caps = false;
    float track = caps ? 0.06f : 0.0f;
    std::vector<HV>* old = g_hvp;
    g_hvp = &g_hvText;
    // red marker on the highlighted entry of a left-hand list (main menu, tabs)
    // (the yellow "selected" colour always; plain white hover only on the big main-menu entries)
    bool selected = (col & 0xFFFFFF) == 0xFBF2BD || ((col & 0xFFFFFF) == 0xFFFFFF && size >= 100);
    if (hi && selected && align == 0 && x1 < g_w * 0.2f) {
        float capH = px * 0.7f;
        Rect(x - px * 0.42f, base - capH, x - px * 0.42f + px * 0.09f, base, Col(C_BLOOD, a));
    }
    // primary action: the right-hand button of a panel's bottom bar (APLICAR, ELEGIR, JUGAR...)
    if (caps && y1 > g_h * 0.82f && (x1 + x2) * .5f > g_w * 0.5f && (align == 2 || x1 > g_w * 0.55f) && size < 100) {
        float tw = TextWidth(g_gBar, s, px, track), lx = align == 1 ? x - tw / 2 : align == 2 ? x - tw : x;
        float k = g_h / 720.0f, padX = 22 * k, padY = 7 * k, capH = px * 0.7f, t = fmaxf(1, k * 0.75f);
        float bx0 = lx - padX, bx1 = lx + tw + padX, by0 = base - capH - padY, by1 = base + padY;
        Rect(bx0, by0, bx1, by1, Col(0xB3160F, .25f * a));
        DWORD bc = Col(C_BLOOD, .8f * a);
        Rect(bx0, by0, bx1, by0 + t, bc); Rect(bx0, by1 - t, bx1, by1, bc);
        Rect(bx0, by0, bx0 + t, by1, bc); Rect(bx1 - t, by0, bx1, by1, bc);
        rgb = C_BONE;
    }
    Text(g_gBar, s, x, base, px, Col(rgb, a), align, track);
    if (g_dlgHavePanel) {                                 // message box: remember where its texts are
        float tw = TextWidth(g_gBar, s, px, track), lx = align == 1 ? x - tw / 2 : align == 2 ? x - tw : x;
        DialogTextBox(lx, base - px * 0.8f, lx + tw, base + px * 0.15f);
    }
    // our credit, on its own line above the publisher's copyright (which stays as it is)
    if ((col & 0xFFFFFF) == 0xB1AFAF && strstr(s, "REL Games")) {
        // clickable: opens the author's Steam profile (hover: brighter and underlined)
        const char* cr = Tr(CREDIT);
        float cb = base - px * 1.45f, tw = TextWidth(g_gBar, cr, px, track);
        float lx = align == 1 ? x - tw / 2 : align == 2 ? x - tw : x;
        g_creditRect[0] = lx; g_creditRect[1] = cb - px * 0.9f; g_creditRect[2] = lx + tw; g_creditRect[3] = cb + px * 0.35f;
        g_creditSeen = GetTickCount();
        bool hover = g_cursorX >= g_creditRect[0] && g_cursorX <= g_creditRect[2] && g_cursorY >= g_creditRect[1] && g_cursorY <= g_creditRect[3];
        // official Steam icon after the text, on its own (Valve's guidelines: no frame, no recolouring);
        // the text moves left to make room, the icon goes from grey to white on hover
        float ih = px * 1.05f, gap = px * 0.45f;
        const HudGlyph& sg = g_gNamed[HN_STEAM];
        float iw = sg.ok ? ih * sg.w / sg.h : 0;
        float shift = sg.ok ? iw + gap : 0;
        if (align == 2) { lx -= shift; g_creditRect[0] = lx; }
        else g_creditRect[2] = lx + tw + shift;
        hover = g_cursorX >= g_creditRect[0] && g_cursorX <= g_creditRect[2] && g_cursorY >= g_creditRect[1] && g_cursorY <= g_creditRect[3];
        Text(g_gBar, cr, lx, cb, px, Col(hover ? C_BONE : rgb, a), 0, track);
        if (sg.ok) {
            float ix = lx + tw + gap, iy = cb - px * 0.36f;
            PushQuad(ix, iy - ih * 0.5f, ix + iw, iy + ih * 0.5f, sg, Col(hover ? 0xFFFFFF : C_DIM, a));
        }
    }
    g_hvp = old;
    return true;
}

static void SkinPanel(IDirect3DDevice9* d, const float* b, bool inner)
{
    float k = g_h / 720.0f;
    float x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3], t = fmaxf(1, k * 0.75f);
    if (inner) {
        Rect(x0, y0, x1, y1, Col(0x000000, .32f));
        Rect(x0, y0, x1, y0 + t, Col(C_BONE, .16f)); Rect(x0, y1 - t, x1, y1, Col(C_BONE, .16f));
        Rect(x0, y0, x0 + t, y1, Col(C_BONE, .16f)); Rect(x1 - t, y0, x1, y1, Col(C_BONE, .16f));
    } else {
        Rect(x0, y0, x1, y1, Col(0x0E0B0A, .8f));
        Rect(x0, y0, x1, y0 + t, Col(C_BONE, .28f)); Rect(x0, y1 - t, x1, y1, Col(C_BONE, .28f));
        Rect(x0, y0, x0 + t, y1, Col(C_BONE, .28f)); Rect(x1 - t, y0, x1, y1, Col(C_BONE, .28f));
        // red line along the top, fading out towards the right
        int seg = 24;
        for (int i = 0; i < seg; i++) {
            float u0 = i / (float)seg, u1 = (i + 1) / (float)seg;
            float aa = fmaxf(0, 1 - (u0 + u1) * .5f / 0.7f);
            Rect(x0 + (x1 - x0) * u0, y0, x0 + (x1 - x0) * u1, y0 + 2 * t, Col(C_BLOOD, aa));
        }
        float c = 18 * k, w = 2 * t;
        DWORD cc = Col(C_BONE, .6f);
        Rect(x0, y0, x0 + c, y0 + w, cc); Rect(x0, y0, x0 + w, y0 + c, cc);
        Rect(x1 - c, y0, x1, y0 + w, cc); Rect(x1 - w, y0, x1, y0 + c, cc);
        Rect(x0, y1 - w, x0 + c, y1, cc); Rect(x0, y1 - c, x0 + w, y1, cc);
        Rect(x1 - c, y1 - w, x1, y1, cc); Rect(x1 - w, y1 - c, x1, y1, cc);
    }
    FlushHud(d);
}

static void SkinSlider(IDirect3DDevice9* d, const float* r, float v)
{
    float k = g_h / 720.0f, cy = (r[1] + r[3]) * .5f, h = 4 * k * 0.5f;
    Rect(r[0], cy - h, r[2], cy + h, Col(C_BONE, .16f));
    if (v < 0) { FlushHud(d); return; }
    float xe = r[0] + (r[2] - r[0]) * fminf(1, fmaxf(0, v));
    int seg = 16;
    for (int i = 0; i < seg; i++) {                      // dark red -> blood red
        float u0 = i / (float)seg, u1 = (i + 1) / (float)seg;
        DWORD c0 = 0x6D0B07, c1 = C_BLOOD;
        int rr = (int)(((c0 >> 16) & 255) + (((c1 >> 16) & 255) - (int)((c0 >> 16) & 255)) * u1);
        int gg = (int)(((c0 >> 8) & 255) + (((c1 >> 8) & 255) - (int)((c0 >> 8) & 255)) * u1);
        int bb = (int)((c0 & 255) + ((c1 & 255) - (int)(c0 & 255)) * u1);
        Rect(r[0] + (xe - r[0]) * u0, cy - h, r[0] + (xe - r[0]) * u1, cy + h, Col((rr << 16) | (gg << 8) | bb, 1));
    }
    float ds = 12 * k * 0.5f * 1.414f;
    PushQuadRot(xe, cy, 12 * k * 0.5f * 2, 12 * k * 0.5f * 2, 0.785f, WhiteTexel(), Col(C_BONE, 1));
    (void)ds;
    FlushHud(d);
}

// Opacity the game gives this draw: vertex colour, texture factor or material alpha, as used by
// stage 0's alpha operation (the texture's own alpha is not included).
static float GameDrawAlpha(IDirect3DDevice9* d, INT base, UINT minIdx)
{
    DWORD fvf = 0, aop = 0, a1 = 0, a2 = 0, light = 0, src = 0, tf = 0;
    d->GetFVF(&fvf);
    d->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG1, &a1);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG2, &a2);
    d->GetRenderState(D3DRS_LIGHTING, &light);
    d->GetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, &src);
    d->GetRenderState(D3DRS_TEXTUREFACTOR, &tf);
    auto used = [&](DWORD arg) {
        if (aop == D3DTOP_DISABLE) return false;
        if (aop == D3DTOP_SELECTARG1) return (a1 & D3DTA_SELECTMASK) == arg;
        if (aop == D3DTOP_SELECTARG2) return (a2 & D3DTA_SELECTMASK) == arg;
        return (a1 & D3DTA_SELECTMASK) == arg || (a2 & D3DTA_SELECTMASK) == arg;
    };
    float a = 1;
    if (used(D3DTA_TFACTOR)) a *= ((tf >> 24) & 255) / 255.0f;
    if (used(D3DTA_DIFFUSE) || used(D3DTA_CURRENT)) {
        if (light && !((fvf & D3DFVF_DIFFUSE) && src == D3DMCS_COLOR1)) {
            D3DMATERIAL9 m;
            if (SUCCEEDED(d->GetMaterial(&m))) a *= fminf(1, fmaxf(0, m.Diffuse.a));
        } else if (fvf & D3DFVF_DIFFUSE) {
            UINT off = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW ? 16 : 12;
            if (fvf & D3DFVF_NORMAL) off += 12;
            if (fvf & D3DFVF_PSIZE) off += 4;
            IDirect3DVertexBuffer9* vb = nullptr;
            UINT o = 0, stride = 0;
            d->GetStreamSource(0, &vb, &o, &stride);
            void* p = nullptr;
            if (vb && stride > off && base >= 0 &&
                SUCCEEDED(vb->Lock(o + ((UINT)base + minIdx) * stride, stride, &p, D3DLOCK_READONLY)) && p) {
                a *= ((*(DWORD*)((char*)p + off) >> 24) & 255) / 255.0f;
                vb->Unlock();
            }
            if (vb) vb->Release();
        }
    }
    return a;
}

// Draw hook: true when the draw was replaced by the skin.
static bool SkinDraw(IDirect3DDevice9* d, bool indexed, D3DPRIMITIVETYPE pt, INT b, UINT mn, UINT nv, UINT si, UINT n,
                     bool* drawThenDim)
{
    *drawThenDim = false;
    if (!SkinOn() || g_inFX || Is2D(d) || fabsf(g_curProjM._34) > 0.5f || !LoadHudGfx(d)) return false;
    if (g_drawsSince3D > 0) {                           // in game: only dialogs (pause, mission end)
    }
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    D3DSURFACE_DESC td = {};
    if (t) {
        if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td);
        t->Release();
    }
    if (!indexed) return false;
    float bb[4];
    if (!DrawBBox(d, b, mn, nv, bb)) return false;
    float w = bb[2] - bb[0], h = bb[3] - bb[1];
    if (w >= g_w * 0.98f && h >= g_h * 0.98f) { *drawThenDim = g_drawsSince3D == 0; g_skinFrameBg = true; return false; }   // background art
    if (td.Width != 64 || td.Height != 64) return false;
    if (bb[0] > g_w || bb[2] < 0) return true;          // off-screen parts (scroll bars)
    if (h < g_h * 0.05f && w > g_w * 0.04f) {           // slider: track, then its thumb (a block that moves)
        float cyb = (bb[1] + bb[3]) * .5f, cyt = (g_skinBar[1] + g_skinBar[3]) * .5f;
        bool thumb = g_skinBarSet && fabsf(cyb - cyt) < h && bb[0] >= g_skinBar[0] - 2 && bb[2] <= g_skinBar[2] + 2 &&
                     w < (g_skinBar[2] - g_skinBar[0]) * 0.7f;
        if (!thumb) {
            if (w < g_w * 0.08f) return false;
            memcpy(g_skinBar, bb, sizeof(bb));
            g_skinBarSet = true;
            // the track runs between the thumb centres at both ends, so our marker sits where the
            // game's (invisible) thumb really is and grabbing it follows the mouse
            float tr[4] = { bb[0] + g_skinThumbW * .5f, bb[1], bb[2] - g_skinThumbW * .5f, bb[3] };
            SkinSlider(d, tr, -1);                        // track only
        } else {
            g_skinThumbW = w;
            float span = (g_skinBar[2] - g_skinBar[0]) - w;
            float v = span > 1 ? (bb[0] - g_skinBar[0]) / span : 0;
            float tr[4] = { g_skinBar[0] + w * .5f, g_skinBar[1], g_skinBar[2] - w * .5f, g_skinBar[3] };
            SkinSlider(d, tr, v);
        }
        return true;
    }
    if (h < g_h * 0.05f) return false;
    if (w > g_w * 0.08f && h > g_h * 0.06f) {
        bool inner = false;
        for (int i = 0; i < g_skinPanelN; i++)
            if (bb[0] >= g_skinPanels[i][0] - 2 && bb[2] <= g_skinPanels[i][2] + 2 && bb[1] >= g_skinPanels[i][1] - 2 && bb[3] <= g_skinPanels[i][3] + 2)
                inner = true;
        if (!inner && g_skinPanelN < 16) memcpy(g_skinPanels[g_skinPanelN++], bb, sizeof(bb));
        float ga = GameDrawAlpha(d, b, mn);
        static float lastGa = -1;
        static int gaLogs;
        if (fabsf(ga - lastGa) > 0.2f && gaLogs < 12) { gaLogs++; Log("Menu: panel con opacidad %.2f", ga); }
        lastGa = ga;
        float pr[4] = { bb[0], bb[1], bb[2], bb[3] };
        if (IsStdMsg()) {
            if (!inner) { memcpy(g_dlgPanel, bb, sizeof(bb)); g_dlgHavePanel = true; }
            if (g_dlgPrevOk) {                          // fit the panel to the texts (with a margin)
                float k = g_h / 720.0f, px = inner ? 26 * k : 40 * k, pt = inner ? 18 * k : 30 * k, pb = inner ? 16 * k : 26 * k;
                float r[4] = { g_dlgPrev[0] - px, g_dlgPrev[1] - pt, g_dlgPrev[2] + px, g_dlgPrev[3] + pb };
                if (r[2] - r[0] > g_w * 0.18f) {          // sane box only
                    pr[0] = fmaxf(bb[0], r[0]); pr[1] = fmaxf(bb[1], r[1]);
                    pr[2] = fminf(bb[2], r[2]); pr[3] = fminf(bb[3], r[3]);
                }
            }
        }
        if (ga > 0.01f) {                               // the game fades dialogs out by alpha, then keeps drawing them
            g_flushAlpha = ga;
            SkinPanel(d, pr, inner);
            g_flushAlpha = 1;
        }
        return true;
    }
    return false;
}

// Dark gradient behind the left-hand menu: rgba(8,6,5,.82) -> .4 at 45 % -> 0 at 70 %. Two quads with
// per-vertex alpha, so it is smooth (stepped bands showed as vertical lines on the new art).
static void GradQuad(float x0, float x1, float H, DWORD c0, DWORD c1)
{
    HudGlyph g = WhiteTexel();
    HV v[4] = { { x0 - .5f, -.5f, 0, 1, c0, g.u0, g.v0 }, { x1 - .5f, -.5f, 0, 1, c1, g.u1, g.v0 },
                { x0 - .5f, H - .5f, 0, 1, c0, g.u0, g.v1 }, { x1 - .5f, H - .5f, 0, 1, c1, g.u1, g.v1 } };
    std::vector<HV>& o = *g_hvp;
    o.push_back(v[0]); o.push_back(v[1]); o.push_back(v[2]);
    o.push_back(v[2]); o.push_back(v[1]); o.push_back(v[3]);
}

static void SkinBackgroundDim(IDirect3DDevice9* d)
{
    float W = (float)g_w, H = (float)g_h;
    GradQuad(0, W * .45f, H, Col(0x080605, .82f), Col(0x080605, .4f));
    GradQuad(W * .45f, W * .7f, H, Col(0x080605, .4f), Col(0x080605, 0));
    FlushHud(d);
}

// Before Present: the collected texts on top of everything.
static void MenuSkinFrame(IDirect3DDevice9* d)
{
    if (!g_hvText.empty()) FlushVec(d, g_hvText);
    g_dlgPrevOk = g_dlgHaveText;
    if (g_dlgHaveText) memcpy(g_dlgPrev, g_dlgText, sizeof(g_dlgPrev));
    g_dlgHavePanel = g_dlgHaveText = false;
    g_skinPanelN = 0;
    g_skinBarSet = false;
}

static struct SkinInit {
    SkinInit() { g_skinTextFn = SkinText; g_skinPanelFn = SkinPanel; g_skinSliderFn = SkinSlider; g_skinOnFn = SkinOn; }
} g_skinInit;
