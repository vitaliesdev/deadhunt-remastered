// DeadhuntFX - HUD and menu layout for wide screens.
//
// The game lays out its 2D interface for 800x600 and stretches it to the screen
// (x * W/800, y * H/600). On 21:9 everything is ~1.8x too wide. Each 2D draw is
// remapped with x' = ax*x + bx, y' = ay*y + by:
//  - uniform scale (the original proportions),
//  - in game: anchored to the left / right edge or centered, by where the piece is,
//  - in menus (no 3D world this frame): the whole menu as one block, centered, cursor
//    included, so the game's click areas still match what is on screen,
//  - full-screen pictures: cover the screen without distortion.
// Screen-space vertices are rewritten; 2D-camera draws get an adjusted projection.
// Included from main.cpp right before the draw hooks (after hud_capture.inl).

struct HudXf { float ax, bx, ay, by; };

// The game sizes its text by the screen WIDTH (both axes), everything else by width and
// height separately: on wide screens text is ~1.8x too big. Text is shrunk uniformly
// around the point it is aligned to. Alignment is guessed from the other text boxes of
// the previous frame (menus are static): shared left edge = left aligned, shared right
// edge = right aligned, otherwise centered.
struct TextBox { float x0, y0, x1, y1; };
static TextBox g_textPrev[256], g_textCur[256];
static int g_textPrevN, g_textCurN;
static float g_alignTol = 50;   // px; set from the screen width

static void TextFrameEnd()
{
    memcpy(g_textPrev, g_textCur, sizeof(TextBox) * g_textCurN);
    g_textPrevN = g_textCurN;
    g_textCurN = 0;
}

// Panels (2D-camera boxes) of the previous frame, to clip list rows that overflow them.
static TextBox g_panelPrev[128], g_panelCur[128];
static int g_panelPrevN, g_panelCurN;

static bool RowOutsideList(const float bb[4])
{
    // first row of this column (same left edge) in the previous frame
    const TextBox* first = nullptr;
    int members = 0;
    for (int i = 0; i < g_textPrevN; i++) {
        const TextBox& o = g_textPrev[i];
        if (fabsf(o.x0 - bb[0]) < g_alignTol) {
            members++;
            if (!first || o.y0 < first->y0) first = &o;
        }
    }
    if (members < 4 || !first) return false;           // only real lists
    float fx = (first->x0 + first->x1) * 0.5f, fy = (first->y0 + first->y1) * 0.5f;
    const TextBox* box = nullptr;                       // innermost panel holding the first row
    for (int i = 0; i < g_panelPrevN; i++) {
        const TextBox& p = g_panelPrev[i];
        if (fx >= p.x0 && fx <= p.x1 && fy >= p.y0 && fy <= p.y1)
            if (!box || (p.x1 - p.x0) * (p.y1 - p.y0) < (box->x1 - box->x0) * (box->y1 - box->y0)) box = &p;
    }
    if (!box) return false;
    float cy = (bb[1] + bb[3]) * 0.5f;
    if (cy <= box->y1) return false;
    // overflow rows sit exactly on the list's row grid; other labels below the box do not
    float spacing = 1e9f;
    for (int i = 0; i < g_textPrevN; i++) {
        const TextBox& o = g_textPrev[i];
        float d = o.y0 - first->y0;
        if (fabsf(o.x0 - bb[0]) < g_alignTol && d > 4 && d < spacing) spacing = d;
    }
    if (spacing > 1e8f) return false;
    float k = (bb[1] - first->y0) / spacing;
    return fabsf(k - floorf(k + 0.5f)) * spacing < 4;
}

static void PanelFrameEnd()
{
    memcpy(g_panelPrev, g_panelCur, sizeof(TextBox) * g_panelCurN);
    g_panelPrevN = g_panelCurN;
    g_panelCurN = 0;
}


static float TextAnchorX(const float bb[4], bool menu)
{
    int left = 0, right = 0;
    for (int i = 0; i < g_textPrevN; i++) {
        const TextBox& o = g_textPrev[i];
        if (fabsf(o.y0 - bb[1]) < 4 && fabsf(o.x0 - bb[0]) < 4) continue;   // itself
        if (fabsf(o.x0 - bb[0]) < g_alignTol) left++;
        if (fabsf(o.x1 - bb[2]) < g_alignTol) right++;
    }
    if (menu) return (right >= 2 && right > left) ? bb[2] : bb[0];   // menus: keep each line's own edge
    if (left && left >= right) return bb[0];
    if (right > left) return bb[2];
    return (bb[0] + bb[2]) * 0.5f;
}

static bool g_hudRefScreen(UINT& W, UINT& H, IDirect3DDevice9* d)
{
    D3DVIEWPORT9 vp;
    if (FAILED(d->GetViewport(&vp)) || !vp.Width || !vp.Height) return false;
    W = vp.Width; H = vp.Height;
    return true;
}

// Rules (see the header): only the interface phase of game frames is touched.
//  - pieces on the left/right edge: recolocated with the real proportions, anchored to that edge
//  - centered text: real proportions around its own center (its click area stays where it was)
//  - centered panels, the cursor, full-screen pictures: untouched
//  - menus (no 3D world this frame): untouched unless [HUD] Menus=1
static HudXf HudTransformFor(const float bb[4], UINT W, UINT H, bool menu, bool cursor, bool text)
{
    if (text && g_textCurN < 256) g_textCur[g_textCurN++] = { bb[0], bb[1], bb[2], bb[3] };
    float sx = W / 800.0f, sy = H / 600.0f;
    float s = sy * S.hudScale;
    float a = s / sx;
    HudXf t = { 1, 0, 1, 0 };
    float w = bb[2] - bb[0], h = bb[3] - bb[1];

    if (cursor) return t;
    if (w >= W * 0.98f && h >= H * 0.98f) {
        // full-screen picture: keep the width, grow the height to undo the stretch (cover)
        if (S.hudCover && sx > sy) {
            t.ay = sx / sy;
            t.by = H * 0.5f * (1 - t.ay);
        }
        return t;
    }
    if (W <= H * 4.0f / 3.0f + 1) return t;            // 4:3 or narrower: nothing to fix

    // text that is not glued to a screen edge: real size, around its alignment point
    // in game, pieces in the outer side zones belong to an edge group (radar + its dots, bars)
    float cx = (bb[0] + bb[2]) * 0.5f;
    bool leftSide = bb[0] < W * 0.1f || cx < W * 0.3f, rightSide = bb[2] > W * 0.9f || cx > W * 0.7f;
    bool edge = !menu && (leftSide || rightSide);
    if (text && !edge && S.hudTextFix) {
        float k = a / S.hudScale * S.hudTextScale * S.hudScale;   // = a * TextScale
        float px = TextAnchorX(bb, menu), py = (bb[1] + bb[3]) * 0.5f;
        return HudXf{ k, px * (1 - k), k, py * (1 - k) };
    }
    if (menu && !S.hudMenus) return t;                  // menu panels/buttons stay (click areas)

    float areaW = S.hudArea > 0 ? fminf((float)W, H * S.hudArea) : (float)W;
    float inset = (W - areaW) * 0.5f;
    t.ax = a;
    float center = W * 0.5f - 400.0f * s;
    if (menu)                      t.bx = center;
    else if (leftSide)             t.bx = inset;                       // left edge
    else if (rightSide)            t.bx = W - inset - 800.0f * s;      // right edge
    else if (text)                 t.bx = (bb[0] + bb[2]) * 0.5f * (1 - a);   // shrink in place
    else                           return HudXf{ 1, 0, 1, 0 };          // centered panel: untouched

    t.ay = S.hudScale;
    if (bb[1] < H * 0.1f)          t.by = 0;                           // top
    else if (bb[3] > H * 0.9f)     t.by = H * (1 - S.hudScale);        // bottom
    else                           t.by = H * 0.5f * (1 - S.hudScale); // middle
    return t;
}

// The 2D camera the game uses for its interface maps 800x600-style units non-uniformly.
static bool IsStretched2DCamera(UINT W, UINT H)
{
    if (fabsf(g_curProjM._34) > 0.5f) return false;    // perspective
    float px = fabsf(g_curProjM._11) * W, py = fabsf(g_curProjM._22) * H;
    return px > 0 && py > 0 && fabsf(px / py - 1) > 0.05f;
}

static void ApplyToVerts(unsigned char* v, UINT count, UINT stride, const HudXf& t)
{
    for (UINT i = 0; i < count; i++) {
        float* p = (float*)(v + (size_t)i * stride);
        p[0] = p[0] * t.ax + t.bx;
        p[1] = p[1] * t.ay + t.by;
    }
}

static D3DMATRIX ProjFor(const HudXf& t, UINT W, UINT H)
{
    // clip-space version of the screen remap, applied after the game's projection
    float tx = (t.ax - 1) + 2 * t.bx / W;
    float ty = (1 - t.ay) - 2 * t.by / H;
    D3DMATRIX r;
    const D3DMATRIX& p = g_curProjM;
    for (int i = 0; i < 4; i++) {
        r.m[i][0] = p.m[i][0] * t.ax + p.m[i][3] * tx;
        r.m[i][1] = p.m[i][1] * t.ay + p.m[i][3] * ty;
        r.m[i][2] = p.m[i][2];
        r.m[i][3] = p.m[i][3];
    }
    return r;
}

static bool HudReject(int r) { if (r < 16) g_hudRej[r]++; return false; }

// Draws the call itself (remapped) and returns true, or returns false to let the
// hook forward it unchanged. upVerts/upIdx are the caller's pointers for *UP calls.
static bool HudLayoutDraw(IDirect3DDevice9* d, bool indexed, bool up, D3DPRIMITIVETYPE pt,
                          INT base, UINT minIdx, UINT numV, UINT start, UINT prims,
                          const void* upIdx, D3DFORMAT upIfmt, const void* upVerts, UINT upStride)
{
    if (g_inFX || !S.hudFix || !S.enabled) return HudReject(1);
    IDirect3DVertexShader9* vs = nullptr;
    d->GetVertexShader(&vs);
    if (vs) { vs->Release(); return HudReject(2); }
    UINT W, H;
    if (!g_hudRefScreen(W, H, d)) return HudReject(3);
    bool rhw = Is2D(d);
    bool cam2d = !rhw && IsStretched2DCamera(W, H);
    if (!rhw && !cam2d) return HudReject(4);
    bool menu = g_drawsSince3D == 0;
    if (!menu && !g_fxDone) return HudReject(5);               // in game only the interface phase (not effects)

    // Vertex range used by the draw. Element 0 of 'src' is vertex 'base' (indexed) or
    // 'start' (non-indexed); user-pointer draws start at the caller's pointer.
    UINT first = indexed ? minIdx : 0;
    UINT count = indexed ? numV : VertsForPrims(pt, prims);
    UINT total = first + count;

    IDirect3DVertexBuffer9* vb = nullptr;
    UINT off = 0, stride = up ? upStride : 0;
    const unsigned char* src = nullptr;
    unsigned char* locked = nullptr;
    if (up) {
        src = (const unsigned char*)upVerts;
    } else {
        d->GetStreamSource(0, &vb, &off, &stride);
        if (!vb || !stride || (indexed && base < 0)) { if (vb) vb->Release(); return HudReject(6); }
        void* p = nullptr;
        UINT lockFrom = off + (indexed ? (UINT)base : start) * stride;
        if (FAILED(vb->Lock(lockFrom, total * stride, &p, D3DLOCK_READONLY)) || !p) { vb->Release(); return HudReject(7); }
        locked = (unsigned char*)p;
        src = locked;
    }
    float bb[4];
    HudBBox(src + (size_t)first * stride, count, stride, rhw, bb);
    bool cursor = up && prims == 2 && fabsf((bb[2] - bb[0]) - (bb[3] - bb[1])) < 2 && bb[2] - bb[0] <= 130;
    bool text = false;   // the game's fonts are 512x512 A8R8G8B8 atlases
    IDirect3DBaseTexture9* tex0 = nullptr;
    d->GetTexture(0, &tex0);
    if (tex0) {
        D3DSURFACE_DESC td;
        if (tex0->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9*)tex0)->GetLevelDesc(0, &td)))
            text = td.Width == 512 && td.Height == 512 && td.Format == D3DFMT_A8R8G8B8;
        tex0->Release();
    }
    g_alignTol = W * 0.015f;
    // [Debug] HideHUD=1: draw no interface at all except full-screen pictures (clean screenshots)
    if (S.hideHud && !(bb[2] - bb[0] >= W * 0.98f && bb[3] - bb[1] >= H * 0.98f)) {
        if (locked) vb->Unlock();
        if (vb) vb->Release();
        return true;
    }
    if (!text && !cursor && cam2d && g_panelCurN < 128 && (bb[2] - bb[0]) > W * 0.05f && (bb[3] - bb[1]) > H * 0.05f)
        g_panelCur[g_panelCurN++] = { bb[0], bb[1], bb[2], bb[3] };
    if (menu && text && S.hudTextFix && RowOutsideList(bb)) {
        if (g_textCurN < 256) g_textCur[g_textCurN++] = { bb[0], bb[1], bb[2], bb[3] };   // keep it for the column
        if (locked) vb->Unlock();
        if (vb) vb->Release();
        return true;                                    // row below its list box: not drawn
    }
    HudXf t = HudTransformFor(bb, W, H, menu, cursor, text);
    if (t.ax == 1 && t.bx == 0 && t.ay == 1 && t.by == 0) {
        if (locked) vb->Unlock();
        if (vb) vb->Release();
        return HudReject(8);
    }
    if (menu) g_hudMenuDraws++; else g_hudDraws++;

    bool done = false;
    if (cam2d) {
        if (locked) vb->Unlock();
        D3DMATRIX np = ProjFor(t, W, H), op = g_curProjM;
        oSetTransform(d, D3DTS_PROJECTION, &np);
        if (up) {
            if (indexed) oDIPUP(d, pt, minIdx, numV, prims, upIdx, upIfmt, upVerts, upStride);
            else oDPUP(d, pt, prims, upVerts, upStride);
            d->SetStreamSource(0, nullptr, 0, 0);
        } else if (indexed) oDIP(d, pt, base, minIdx, numV, start, prims);
        else oDP(d, pt, start, prims);
        oSetTransform(d, D3DTS_PROJECTION, &op);
        done = true;
    } else {
        // screen-space vertices: draw a remapped copy from memory
        unsigned char* copy = (unsigned char*)malloc((size_t)total * stride);
        if (copy) {
            memcpy(copy, src, (size_t)total * stride);
            ApplyToVerts(copy + (size_t)first * stride, count, stride, t);
        }
        if (locked) vb->Unlock();
        if (copy) {
            if (indexed) {
                const void* idx = upIdx;
                D3DFORMAT ifmt = upIfmt;
                void* ilock = nullptr;
                IDirect3DIndexBuffer9* ib = nullptr;
                if (!up) {
                    d->GetIndices(&ib);
                    D3DINDEXBUFFER_DESC idesc;
                    UINT isz = 2;
                    if (ib && SUCCEEDED(ib->GetDesc(&idesc))) { ifmt = idesc.Format; isz = ifmt == D3DFMT_INDEX32 ? 4 : 2; }
                    UINT nIdx = pt == D3DPT_TRIANGLELIST ? prims * 3 : prims + 2;
                    if (ib && SUCCEEDED(ib->Lock(start * isz, nIdx * isz, &ilock, D3DLOCK_READONLY))) idx = ilock;
                }
                if (idx) {
                    oDIPUP(d, pt, minIdx, numV, prims, idx, ifmt, copy, stride);
                    done = true;
                }
                if (ilock) ib->Unlock();
                if (!up && ib) { d->SetIndices(ib); }
                if (ib) ib->Release();
            } else {
                oDPUP(d, pt, prims, copy, stride);
                done = true;
            }
            free(copy);
        }
        // *UP draws reset stream 0: give the game back its buffer
        if (!up && done) d->SetStreamSource(0, vb, off, stride);
        else if (up && done) d->SetStreamSource(0, nullptr, 0, 0);
    }
    if (vb) vb->Release();
    return done;
}
