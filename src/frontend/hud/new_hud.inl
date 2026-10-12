// DeadhuntFX - new in-game HUD (replaces the game's own HUD window drawing).
//
// The game's HUD is a GUI window; its draw method is 0x4c8b60 (thiscall, no arguments). A detour
// runs the original inside a "HUD pass" flag: every draw and text it issues is swallowed (the
// radar disc and enemy dots are recorded on the way, and the centre messages are kept), then the
// new HUD is drawn with our own atlas (DeadhuntFX/hud_atlas.bin, built by tools/generators/hud_atlas.py:
// Oxanium glyphs, icons and shapes, each with a halo).
//
// Data (engine):
//   health / max     HUD window +0x1d4 / +0x1d8 (floats, set by 0x4c7e60)
//   score            HUD window +0x220 (set by 0x4c72f0, grows with the combo multiplier)
//   kills            [[0x6b71c4]+0x38]+0xc
//   time             [0x6b71c4]+0x5f0 (float seconds)
//   grenades         HUD window +0x1cc (3rd argument of 0x4c7060)
//   runes            [[0x6b71c4]+0x48]+0x44
//   weapon / ammo    arguments of 0x4c8140 (weapon id, magazine, capacity), recorded by a detour
//   hit taken        0x4c8b40 is called
// Included from main.cpp after advanced_page.inl.

static DWORD g_hudWin;                 // the game's HUD window object (last seen)
static bool  g_inGameHud;              // inside the game's HUD draw method
static int   g_hudWeapon = 1, g_hudMag = -1, g_hudCap = 1;
static DWORD g_hudHitTick, g_hudKillTick;
static int   g_hudLastKills = -1;
static bool  g_hudReloading;

// ---------------------------------------------------------------- atlas

struct HudGlyph { float u0, v0, u1, v1, w, h, xo, yo, adv, size; bool ok; };
static HudGlyph g_gOsw[256], g_gBar[256], g_gNamed[32];
enum { HN_WHITE, HN_SCRIM, HN_VIGNETTE, HN_RING, HN_DOT, HN_ARROW, HN_ARC, HN_CROSS, HN_SKULL, HN_RUNE,
       HN_GRENADE, HN_W_UZI, HN_W_HK33, HN_W_MOSS, HN_W_JACK, HN_W_M60, HN_W_STEYR, HN_W_DEAGLE, HN_DISK, HN_STEAM };
static IDirect3DTexture9* g_hudTex;
static IDirect3DPixelShader9* g_hudPS;
static IDirect3DStateBlock9* g_hudSB;
static bool g_hudFailed;

static const char* kHudPS =
    "sampler2D s0 : register(s0);\n"
    "float4 halo : register(c0);\n"
    "float4 main(float4 col : COLOR0, float2 uv : TEXCOORD0) : COLOR {\n"
    "  float4 t = tex2D(s0, uv);\n"
    "  float ag = t.a * col.a;\n"
    "  float ah = t.r * halo.a * col.a;\n"
    "  float a = ag + ah * (1 - ag);\n"
    "  float br = saturate(t.g / max(t.a, 0.004));\n"          // shading of shaded icons (G = A elsewhere)
    "  return float4(col.rgb * br * ag + halo.rgb * ah * (1 - ag), a);\n"
    "}\n";

static void ReleaseHudGfx()
{
    if (g_hudTex) { g_hudTex->Release(); g_hudTex = nullptr; }
    if (g_hudPS) { g_hudPS->Release(); g_hudPS = nullptr; }
    if (g_hudSB) { g_hudSB->Release(); g_hudSB = nullptr; }
}

static bool LoadHudGfx(IDirect3DDevice9* d)
{
    if (g_hudTex && g_hudPS) return true;
    if (g_hudFailed) return false;
    g_hudFailed = true;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX\\hud_atlas.bin", g_dir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) { Log("HUD nuevo: falta DeadhuntFX\\hud_atlas.bin"); return false; }
    char magic[4];
    UINT W = 0, H = 0, n = 0;
    fread(magic, 1, 4, f);
    fread(&W, 4, 1, f); fread(&H, 4, 1, f); fread(&n, 4, 1, f);
    if (memcmp(magic, "DHA1", 4) || !W || !H || n > 1000) { fclose(f); Log("HUD nuevo: atlas no valido"); return false; }
#pragma pack(push, 1)
    struct E { unsigned char kind, pad; unsigned short code, x, y, w, h; float xo, yo, adv, size; };
#pragma pack(pop)
    for (UINT i = 0; i < n; i++) {
        E e;
        fread(&e, sizeof(e), 1, f);
        if (e.kind == 3 && g_lang != LANG_RU && g_lang != LANG_UK) continue;   // cp1251 Cyrillic
        if (e.kind == 4 && g_lang != LANG_PL) continue;                         // cp1250 Polish letters
        HudGlyph* g = e.kind == 0 ? &g_gOsw[e.code & 255] : (e.kind == 1 || e.kind >= 3) ? &g_gBar[e.code & 255]
                    : e.code < 32 ? &g_gNamed[e.code] : nullptr;
        if (!g) continue;
        *g = { e.x / (float)W, e.y / (float)H, (e.x + e.w) / (float)W, (e.y + e.h) / (float)H,
               (float)e.w, (float)e.h, e.xo, e.yo, e.adv, e.size, true };
    }
    std::vector<unsigned char> px((size_t)W * H * 4);
    size_t got = fread(px.data(), 1, px.size(), f);
    fclose(f);
    if (got != px.size()) { Log("HUD nuevo: atlas incompleto"); return false; }

    if (FAILED(d->CreateTexture(W, H, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_hudTex, nullptr))) {
        Log("HUD nuevo: no se pudo crear la textura");
        return false;
    }
    // level 0 + box-filtered mip chain
    std::vector<unsigned char> cur = px, next;
    UINT w = W, h = H;
    for (UINT lv = 0; lv < g_hudTex->GetLevelCount(); lv++) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_hudTex->LockRect(lv, &lr, nullptr, 0))) {
            for (UINT y = 0; y < h; y++) memcpy((BYTE*)lr.pBits + (size_t)y * lr.Pitch, &cur[(size_t)y * w * 4], w * 4);
            g_hudTex->UnlockRect(lv);
        }
        if (w == 1 && h == 1) break;
        UINT nw = max(1u, w / 2), nh = max(1u, h / 2);
        next.assign((size_t)nw * nh * 4, 0);
        for (UINT y = 0; y < nh; y++)
            for (UINT x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    UINT x0 = min(x * 2, w - 1), x1 = min(x * 2 + 1, w - 1), y0 = min(y * 2, h - 1), y1 = min(y * 2 + 1, h - 1);
                    UINT s = cur[((size_t)y0 * w + x0) * 4 + c] + cur[((size_t)y0 * w + x1) * 4 + c] +
                             cur[((size_t)y1 * w + x0) * 4 + c] + cur[((size_t)y1 * w + x1) * 4 + c];
                    next[((size_t)y * nw + x) * 4 + c] = (unsigned char)((s + 2) / 4);
                }
        cur.swap(next);
        w = nw; h = nh;
    }

    if (!g_D3DCompile) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) g_D3DCompile = (PFN_FXCompile)GetProcAddress(m, "D3DCompile");
    }
    ID3DBlob *code = nullptr, *err = nullptr;
    if (!g_D3DCompile || FAILED(g_D3DCompile(kHudPS, strlen(kHudPS), "hud", nullptr, nullptr, "main", "ps_2_0", 0, 0, &code, &err)) ||
        FAILED(d->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &g_hudPS))) {
        Log("HUD nuevo: fallo el shader %s", err ? (const char*)err->GetBufferPointer() : "");
        if (code) code->Release();
        if (err) err->Release();
        ReleaseHudGfx();
        return false;
    }
    code->Release();
    if (err) err->Release();
    g_hudFailed = false;
    Log("HUD nuevo: atlas %ux%u, %u elementos", W, H, n);
    return true;
}

// ---------------------------------------------------------------- batching

struct HV { float x, y, z, rhw; DWORD c; float u, v; };
static std::vector<HV> g_hv, g_hvText;
static std::vector<HV>* g_hvp = &g_hv;   // where Push* write (the menu skin also uses a text layer)
static float g_k = 1;                 // reference px (1280x720 mockup) -> screen px

static DWORD Col(DWORD rgb, float a)
{
    a = fminf(1, fmaxf(0, a));
    return ((DWORD)(a * 255 + 0.5f) << 24) | (rgb & 0xFFFFFF);
}

static void PushQuad(float x0, float y0, float x1, float y1, const HudGlyph& g, DWORD c)
{
    HV v[4] = { { x0 - .5f, y0 - .5f, 0, 1, c, g.u0, g.v0 }, { x1 - .5f, y0 - .5f, 0, 1, c, g.u1, g.v0 },
                { x0 - .5f, y1 - .5f, 0, 1, c, g.u0, g.v1 }, { x1 - .5f, y1 - .5f, 0, 1, c, g.u1, g.v1 } };
    std::vector<HV>& o = *g_hvp;
    o.push_back(v[0]); o.push_back(v[1]); o.push_back(v[2]);
    o.push_back(v[2]); o.push_back(v[1]); o.push_back(v[3]);
}

static void PushQuadRot(float cx, float cy, float w, float h, float ang, const HudGlyph& g, DWORD c)
{
    float cs = cosf(ang), sn = sinf(ang);
    float px[4] = { -w / 2, w / 2, -w / 2, w / 2 }, py[4] = { -h / 2, -h / 2, h / 2, h / 2 };
    float uu[4] = { g.u0, g.u1, g.u0, g.u1 }, vv[4] = { g.v0, g.v0, g.v1, g.v1 };
    HV v[4];
    for (int i = 0; i < 4; i++)
        v[i] = { cx + px[i] * cs - py[i] * sn - .5f, cy + px[i] * sn + py[i] * cs - .5f, 0, 1, c, uu[i], vv[i] };
    std::vector<HV>& o = *g_hvp;
    o.push_back(v[0]); o.push_back(v[1]); o.push_back(v[2]);
    o.push_back(v[2]); o.push_back(v[1]); o.push_back(v[3]);
}

static HudGlyph WhiteTexel()
{
    HudGlyph g = g_gNamed[HN_WHITE];
    float du = (g.u1 - g.u0) * .25f, dv = (g.v1 - g.v0) * .25f;
    g.u0 += du; g.u1 -= du; g.v0 += dv; g.v1 -= dv;     // stay well inside the white block
    return g;
}

static void Rect(float x0, float y0, float x1, float y1, DWORD c)
{
    PushQuad(x0, y0, x1, y1, WhiteTexel(), c);
}

// icon whose ink (atlas cell minus its 10px padding) is ih tall, right edge at 'right', centred on cy.
// Returns the ink width.
static float Icon(int id, float right, float cy, float ih, DWORD c)
{
    const HudGlyph& g = g_gNamed[id];
    if (!g.ok) return 0;
    float s = ih / (g.h - 20), w = g.w * s, h = g.h * s;
    PushQuad(right - w + 10 * s, cy - h / 2, right + 10 * s, cy + h / 2, g, c);
    return w - 20 * s;
}

static float TextWidth(const HudGlyph* font, const char* s, float px, float track)
{
    float w = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        const HudGlyph& g = font[*p];
        if (g.ok) w += g.adv * px / g.size + track * px;
    }
    return w > 0 ? w - track * px : 0;
}

// align: 0 left, 1 centre, 2 right; y = baseline. Returns the width.
static float Text(const HudGlyph* font, const char* s, float x, float y, float px, DWORD c, int align = 0, float track = 0)
{
    float w = TextWidth(font, s, px, track);
    if (align == 1) x -= w / 2;
    if (align == 2) x -= w;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        const HudGlyph& g = font[*p];
        if (!g.ok) continue;
        float sc = px / g.size;
        if (g.w > 1) PushQuad(x + g.xo * sc, y + g.yo * sc, x + (g.xo + g.w) * sc, y + (g.yo + g.h) * sc, g, c);
        x += g.adv * sc + track * px;
    }
    return w;
}

static float g_flushAlpha = 1;          // extra opacity for the next flushes (menu skin fades)

static void FlushVec(IDirect3DDevice9* d, std::vector<HV>& hv)
{
    if (hv.empty() || !g_hudTex || !g_hudPS) { hv.clear(); return; }
    if (!g_hudSB) d->CreateStateBlock(D3DSBT_ALL, &g_hudSB);
    if (g_hudSB) g_hudSB->Capture();
    d->SetVertexShader(nullptr);
    d->SetPixelShader(g_hudPS);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    d->SetTexture(0, g_hudTex);
    for (int i = 1; i < 8; i++) d->SetTexture(i, nullptr);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    float halo[4] = { 0, 0, 0, 0.85f };
    d->SetPixelShaderConstantF(0, halo, 1);
    if (g_flushAlpha < 0.999f)                          // fading game element (menu skin): scale coverage
        for (HV& v : hv) v.c = (v.c & 0xFFFFFF) | ((DWORD)(((v.c >> 24) & 255) * g_flushAlpha + 0.5f) << 24);
    oDPUP(d, D3DPT_TRIANGLELIST, (UINT)hv.size() / 3, hv.data(), sizeof(HV));
    if (g_hudSB) g_hudSB->Apply();
    hv.clear();
}

static void FlushHud(IDirect3DDevice9* d) { FlushVec(d, g_hv); }

// ---------------------------------------------------------------- radar capture (from the game's own radar draw)

static float g_radBB[4];
static bool  g_radBG;
static float g_radDots[256];
static int   g_radDotsN;

struct HudMsg { char text[512]; float y; float a; };
static HudMsg g_hudMsgs[4];
static int g_hudMsgN;

// Called for every draw while the game's HUD method runs (the hooks then swallow the draw).
static void HudPassDraw(IDirect3DDevice9* d, bool indexed, D3DPRIMITIVETYPE pt, INT base, UINT minIdx, UINT numV,
                        UINT start, UINT prims, const void* upVerts, UINT upStride)
{
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    D3DSURFACE_DESC td = {};
    if (t) {
        if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td);
        t->Release();
    }
    static int logs;
    if (logs < 40 && IniF(L"Debug", L"HudPassLog", 0) != 0) {
        logs++;
        DWORD fvf = 0;
        d->GetFVF(&fvf);
        Log("paso HUD: %s pt %d prims %u tex %ux%u fvf %X up %d 2d %d", indexed ? "DIP" : "DP", (int)pt, prims, td.Width,
            td.Height, fvf, upVerts != nullptr, (int)Is2D(d));
    }
    bool disc = td.Width == 128 && td.Height == 128 && prims == 2;
    bool dots = g_radBG && !indexed && pt == D3DPT_POINTLIST && prims >= 1 && td.Width == 64 && td.Height == 256;   // one point sprite per enemy
    if ((!disc && !dots) || Is2D(d)) return;
    const unsigned char* src = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT off = 0, stride = upStride, first = indexed ? minIdx : 0;
    UINT count = indexed ? numV : pt == D3DPT_POINTLIST ? prims : VertsForPrims(pt, prims);
    void* p = nullptr;
    if (upVerts) src = (const unsigned char*)upVerts;
    else {
        d->GetStreamSource(0, &vb, &off, &stride);
        if (!vb || !stride) { if (vb) vb->Release(); return; }
        UINT from = off + (indexed ? (UINT)max(0, base) : start) * stride;
        if (FAILED(vb->Lock(from, (first + count) * stride, &p, D3DLOCK_READONLY)) || !p) { vb->Release(); return; }
        src = (const unsigned char*)p;
    }
    float bb[4];
    HudBBox(src + (size_t)first * stride, count, stride, false, bb);
    if (disc && bb[0] < g_w * 0.3f && bb[3] > g_h * 0.6f) {
        memcpy(g_radBB, bb, sizeof(bb));                   // radar disc
        g_radBG = true;
    } else if (dots) {
        float cx = (g_radBB[0] + g_radBB[2]) * .5f, cy = (g_radBB[1] + g_radBB[3]) * .5f;
        float rx = (g_radBB[2] - g_radBB[0]) * .5f, ry = (g_radBB[3] - g_radBB[1]) * .5f;
        for (UINT q = 0; q < count && g_radDotsN < 250; q++) {
            float sp[2];
            HudTransform((const float*)(src + (size_t)(first + q) * stride), sp);
            float x = (sp[0] - cx) / rx, y = (sp[1] - cy) / ry;
            if (x * x + y * y > 1.05f || x * x + y * y < 0.0025f) continue;   // outside, or the player
            g_radDots[g_radDotsN++] = x;
            g_radDots[g_radDotsN++] = y;
        }
    }
    if (vb) { vb->Unlock(); vb->Release(); }
}

// ---------------------------------------------------------------- drawing the new HUD

static int ReadI(DWORD a, int def = 0) { return a && !IsBadReadPtr((void*)a, 4) ? *(int*)a : def; }
static float ReadF(DWORD a, float def = 0) { return a && !IsBadReadPtr((void*)a, 4) ? *(float*)a : def; }
static DWORD ReadP(DWORD a) { return (DWORD)ReadI(a); }

static const DWORD C_SKULL = 0xE2DECB;   // aged bone, shaded by the icon itself
static const DWORD C_BONE = 0xF4F2EE, C_DIM = 0xB9B5AE, C_DANGER = 0xFF3B24, C_ENEMY = 0xFF5A36;

static void DrawDamageDirs(float W, float H);

static void DrawNewHud(IDirect3DDevice9* d)
{
    if (!LoadHudGfx(d)) return;
    float W = (float)g_w, H = (float)g_h;
    // half the mockup size (user request); the safe margin to the edges does not shrink as much
    g_k = H / 720.0f * 0.5f * S.hudScale;
    float k = g_k, t = (GetTickCount() % 1000000) / 1000.0f, m = 30 * H / 720.0f;
    float areaW = S.hudArea > 0 ? fminf(W, H * S.hudArea) : W;
    float inset = (W - areaW) * .5f;
    float L = inset + m, Rx = W - inset - m, By = H - m, Ty = m;

    // ---- data
    DWORD eng = ReadP(0x6b71c4);
    float hp = ReadF(g_hudWin + 0x1d4, 100), hpMax = ReadF(g_hudWin + 0x1d8, 100);
    if (hpMax <= 0) hpMax = 100;
    float hpFrac = fminf(1, fmaxf(0, hp / hpMax));
    // HUD +0x220 is the score (accumulated with the combo multiplier); [[eng+0x38]+0xc] the kills
    int score = ReadI(g_hudWin + 0x220), kills = ReadI(ReadP(eng + 0x38) + 0xc);
    float secs = ReadF(eng + 0x5f0);
    int grenades = ReadI(g_hudWin + 0x1cc), runes = ReadI(ReadP(eng + 0x48) + 0x44);
    if (kills > g_hudLastKills && g_hudLastKills >= 0) g_hudKillTick = GetTickCount();
    g_hudLastKills = kills;
    bool low = hpFrac < 0.3f && hp > 0;
    float sinceHit = (GetTickCount() - g_hudHitTick) / 1000.0f, sinceKill = (GetTickCount() - g_hudKillTick) / 1000.0f;
    char buf[64];

    // ---- red edge: low health breathes, a hit flashes
    float vig = 0;
    if (low) vig = 0.75f + 0.25f * sinf(t * 6.2832f / 1.1f);
    if (sinceHit < 0.45f) vig = fmaxf(vig, 1 - sinceHit / 0.45f);
    if (vig > 0) PushQuad(0, 0, W, H, g_gNamed[HN_VIGNETTE], Col(0x780000, 0.5f * vig));
    DrawDamageDirs(W, H);

    // ---- scrims (no boxes: soft darkening behind each cluster)
    const HudGlyph& sc = g_gNamed[HN_SCRIM];
    PushQuad(W - inset - 520 * k, H - 230 * k, W - inset + 520 * k, H + 230 * k, sc, Col(0, .42f));
    PushQuad(inset - 330 * k * 1.25f, H - 290 * k * 1.25f, inset + 330 * k * 1.25f, H + 290 * k * 1.25f, sc, Col(0, .38f));
    PushQuad(W - inset - 700 * k, 60 * k - 260 * k, W - inset + 700 * k, 60 * k + 260 * k, sc, Col(0, .5f));   // score block

    // ---- radar (bottom-left)
    float rk = 1.25f;                                     // radar 25% larger than the rest of the HUD
    float rr = 70 * k * rk, rcx = L + rr + 10 * k * rk, rcy = By - rr - 10 * k * rk;
    float rs = rr * 128 / 120;
    PushQuad(rcx - rs, rcy - rs, rcx + rs, rcy + rs, g_gNamed[HN_DISK], Col(0x000000, .45f));
    PushQuad(rcx - rs, rcy - rs, rcx + rs, rcy + rs, g_gNamed[HN_RING], Col(C_BONE, .6f));
    float tk = 9 * k * rk, tw = fmaxf(1, 1.2f * k * rk);
    float ti = 6 * k * rk;
    Rect(rcx - tw / 2, rcy - rr + ti - tk, rcx + tw / 2, rcy - rr + ti, Col(C_BONE, .5f));
    Rect(rcx - tw / 2, rcy + rr - ti, rcx + tw / 2, rcy + rr - ti + tk, Col(C_BONE, .5f));
    Rect(rcx - rr + ti - tk, rcy - tw / 2, rcx - rr + ti, rcy + tw / 2, Col(C_BONE, .5f));
    Rect(rcx + rr - ti, rcy - tw / 2, rcx + rr - ti + tk, rcy + tw / 2, Col(C_BONE, .5f));
    // the radar turns with the player (forward = up), so there is no north mark
    const HudGlyph& dg = g_gNamed[HN_DOT];
    float ds = 48 * (3.4f * k * rk) / 13;
    for (int i = 0; i + 1 < g_radDotsN; i += 2) {
        float x = rcx + g_radDots[i] * (rr - 4 * k * rk), y = rcy + g_radDots[i + 1] * (rr - 4 * k * rk);
        PushQuad(x - ds / 2, y - ds / 2, x + ds / 2, y + ds / 2, dg, Col(C_ENEMY, 1));
    }
    float as = 16 * k * rk;
    PushQuad(rcx - as / 2, rcy - as / 2 - 1 * k, rcx + as / 2, rcy + as / 2 - 1 * k, g_gNamed[HN_ARROW], Col(C_BONE, 1));

    // ---- score, kills and time (top right, right-aligned like the dock below)
    {
        char num[24], grp[32];
        sprintf_s(num, "%d", score < 0 ? 0 : score);
        // thousands separator: 12.450 / 12,450 / 12 450
        char sep = g_lang == LANG_EN ? ',' : (g_lang == LANG_RU || g_lang == LANG_UK || g_lang == LANG_FR || g_lang == LANG_PL) ? ' ' : '.';
        int len = (int)strlen(num), o = 0;
        for (int i = 0; i < len; i++) {
            grp[o++] = num[i];
            if ((len - 1 - i) % 3 == 0 && i < len - 1) grp[o++] = sep;
        }
        grp[o] = 0;
        float rx = Rx, top = Ty;
        Text(g_gBar, Tr(HUD_PTS), rx, top + 14 * k, 20 * k, Col(C_DIM, 1), 2, 0.3f);
        float sb = top + 14 * k + 66 * k;                       // score baseline
        Text(g_gOsw, grp, rx, sb, 64 * k, Col(C_BONE, 1), 2);
        float ry = sb + 18 * k, rw = 300 * k, rh = fmaxf(1, 2 * k);
        Rect(rx - rw, ry, rx, ry + rh, Col(C_BONE, .28f));
        char kb[16], tb[16];
        sprintf_s(kb, "%d", kills);
        int sec = (int)secs;
        sprintf_s(tb, "%02d:%02d", sec / 60, sec % 60);
        float l2 = ry + 46 * k;
        Text(g_gOsw, tb, rx, l2, 34 * k, Col(C_DIM, 1), 2, 0.06f);
        float wt = TextWidth(g_gOsw, "88:88", 34 * k, 0.06f);     // fixed slots: nothing moves as the clock runs
        float kx = rx - wt - 34 * k;
        Text(g_gOsw, kb, kx, l2, 34 * k, Col(C_BONE, 1), 2);
        float wk = TextWidth(g_gOsw, kills >= 1000 ? "8888" : "888", 34 * k, 0);
        Icon(HN_SKULL, kx - wk - 10 * k, l2 - 34 * k * 0.38f, 40 * k, Col(C_SKULL, 1));
        if (sinceKill < 1.2f) Text(g_gOsw, "+1", kx + 4 * k, l2 - 30 * k, 22 * k, Col(C_DANGER, 1 - sinceKill / 1.2f));
    }

    // ---- dock (bottom-right): life | rule | ammo, items above
    float numPx = 44 * k, barH = 5 * k, barTop = By - barH, baseL = barTop - 8 * k;
    float numTop = baseL - numPx * 0.82f, midY = (numTop + baseL) * .5f;
    int mag = g_hudMag < 0 ? 0 : g_hudMag, cap = max(1, g_hudCap);
    bool ammoLow = mag <= max(1, cap / 5);
    sprintf_s(buf, "/ %d", cap);
    float wCap = Text(g_gOsw, buf, Rx, baseL - 4 * k, 19 * k, Col(C_DIM, 1), 2);
    sprintf_s(buf, "%d", mag);
    Text(g_gOsw, buf, Rx - wCap - 8 * k, baseL, numPx, Col(ammoLow ? C_DANGER : C_BONE, 1), 2);
    // fixed slots: the magazine number is right-aligned in room for the capacity's digits (at least
    // two), so firing never moves the icon, the rule or the life block
    char slot[8] = "88";
    for (int c = cap, d = 0; c >= 10 && d < 4; c /= 10, d++) if (d >= 1) strcat_s(slot, "8");
    float wMag = TextWidth(g_gOsw, slot, numPx, 0);
    static const int wIcons[8] = { HN_W_UZI, HN_W_UZI, HN_W_HK33, HN_W_MOSS, HN_W_JACK, HN_W_M60, HN_W_STEYR, HN_W_DEAGLE };
    int wi = wIcons[(g_hudWeapon >= 1 && g_hudWeapon <= 7) ? g_hudWeapon : 1];
    float iconRight = Rx - wCap - 8 * k - wMag - 10 * k;
    float wIcon = Icon(wi, iconRight, midY, 34 * k, Col(C_BONE, 1));
    float ammoL = iconRight - wIcon;
    Rect(ammoL, barTop, Rx, barTop + barH, Col(C_BONE, .22f));
    Rect(ammoL, barTop, ammoL + (Rx - ammoL) * fminf(1, mag / (float)cap), barTop + barH, Col(C_DIM, 1));
    if (g_hudReloading && mag == 0)
        Text(g_gBar, Tr(HUD_RELOAD), Rx, By + 18 * k, 13 * k, Col(C_BONE, 1), 2, 0.24f);

    float ruleX = ammoL - 26 * k;
    Rect(ruleX, midY - 23 * k, ruleX + 1 * k, midY + 23 * k, Col(C_BONE, .35f));

    float ph = fmodf(t, 1.1f) / 1.1f;                       // heartbeat: dips at 18% of the cycle
    float pulse = low ? (ph < 0.36f ? 1 - 0.45f * sinf(ph / 0.36f * 3.14159f) : 1) : 1;
    DWORD lifeC = low ? C_DANGER : C_BONE;
    sprintf_s(buf, "%d", (int)ceilf(fmaxf(0, hp)));
    float lifeR = ruleX - 26 * k;
    Text(g_gOsw, buf, lifeR, baseL, numPx, Col(lifeC, pulse), 2);
    float wHp = TextWidth(g_gOsw, hpMax >= 1000 ? "8888" : "888", numPx, 0);    // fixed slot (see the ammo)
    float wCross = Icon(HN_CROSS, lifeR - wHp - 10 * k, midY, 22 * k, Col(lifeC, pulse));
    float lifeL = lifeR - wHp - 10 * k - wCross;
    Rect(lifeL, barTop, lifeR, barTop + barH, Col(C_BONE, .22f));
    Rect(lifeL, barTop, lifeL + (lifeR - lifeL) * hpFrac, barTop + barH, Col(lifeC, pulse));

    float itemY = numTop - 14 * k - 11 * k, itemBase = itemY + 20 * k * 0.4f;
    sprintf_s(buf, "%d", grenades);
    Text(g_gOsw, buf, Rx, itemBase, 20 * k, Col(C_BONE, 1), 2);
    float wg = TextWidth(g_gOsw, "88", 20 * k, 0);
    float wgi = Icon(HN_GRENADE, Rx - wg - 7 * k, itemY, 22 * k, Col(C_BONE, 1));
    float runeR = Rx - wg - 7 * k - wgi - 22 * k;
    sprintf_s(buf, "%d", runes);
    Text(g_gOsw, buf, runeR, itemBase, 20 * k, Col(C_BONE, 1), 2);
    float wr = TextWidth(g_gOsw, "88", 20 * k, 0);
    Icon(HN_RUNE, runeR - wr - 7 * k, itemY, 22 * k, Col(C_BONE, 1));

    // ---- centre messages of the game (level name, pickups), in the new type
    // (the tutorial texts are several lines, split by '^' in the csv and by a line break once the game reads them)
    for (int i = 0; i < g_hudMsgN; i++) {
        char line[sizeof(g_hudMsgs[i].text)];
        const char* s = g_hudMsgs[i].text;
        float y = g_hudMsgs[i].y;
        while (*s) {
            size_t n = strcspn(s, "^\r\n");
            const char* e = s[n] ? s + n : nullptr;
            memcpy(line, s, n);
            line[n] = 0;
            if (n) Text(g_gBar, line, W * .5f, y, 26 * k, Col(C_BONE, g_hudMsgs[i].a), 1, 0.08f);
            y += 34 * k;
            s += n + (e ? 1 : 0);
        }
    }

    // ---- crosshair and hit marker
    float cx = W * .5f, cy = H * .5f;
    float cd = 2.2f * H / 720.0f;                       // centre dot: about 6 px wide at 1440p
    PushQuad(cx - cd, cy - cd, cx + cd, cy + cd, dg, Col(C_BONE, 1));
    if (sinceKill < 0.35f) {
        float a = 1 - sinceKill / 0.35f, o = 11 * k;
        HudGlyph wt = WhiteTexel();
        for (int q = 0; q < 4; q++) {
            float sx = (q & 1) ? 1.f : -1.f, sy = (q & 2) ? 1.f : -1.f;
            PushQuadRot(cx + sx * o, cy + sy * o, 9 * k, 2 * k, (sx * sy > 0 ? 0.785f : -0.785f), wt, Col(C_BONE, a));
        }
    }
    FlushHud(d);
}

// ---------------------------------------------------------------- hooks

typedef int(__fastcall* PFN_HudDraw)(DWORD self, void* edx);

static __declspec(naked) int HudDrawOrig()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x54c
        push 0x4c8b69
        ret
    }
}

static int __stdcall HudDrawCpp(DWORD self)
{
    if (self != g_hudWin) Log("HUD del juego: ventana %08X", self);
    g_hudWin = self;
    IDirect3DDevice9* d = *(IDirect3DDevice9**)0x6b71e4;
    if (!S.newHud || !S.enabled || !d) return ((PFN_HudDraw)(void*)HudDrawOrig)(self, nullptr);
    g_radBG = false;
    g_radDotsN = 0;
    g_hudMsgN = 0;
    g_inGameHud = true;
    int r = ((PFN_HudDraw)(void*)HudDrawOrig)(self, nullptr);
    g_inGameHud = false;
    DrawNewHud(d);
    return r;
}

static __declspec(naked) void HudDrawStub()
{
    __asm {
        push ecx
        call HudDrawCpp
        ret
    }
}

// 0x4c8140(weaponId, magazine, capacity) and 0x4c8b40() (hit taken): record, then run the original
static void __stdcall OnAmmo(DWORD* a)
{
    if ((int)a[1] == 0) g_hudReloading = true;
    else if ((int)a[1] > g_hudMag) g_hudReloading = false;
    g_hudWeapon = (int)a[0];
    g_hudMag = (int)a[1];
    g_hudCap = (int)a[2];
}
// Damage direction. The player's damage function 0x480d50(this = player; a8, aC = attack direction
// vector (from the attacker towards the player), a10, damage, a18) runs first; 0x4c8b40 then
// confirms that the hit counted. The indicator keeps the world direction, so it turns with the view.
struct DamageDir { float x, z; DWORD tick; };
static DamageDir g_dmgDirs[4];
static float g_dmgPending[3];
static bool g_dmgHavePending;
static int g_dmgLogged;

static void __stdcall OnDamage(DWORD* a)
{
    const float* v = (const float*)a[1];
    g_dmgHavePending = false;
    if (!v || IsBadReadPtr(v, 12)) return;
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(len > 1e-4f) || len > 1e6f) return;
    for (int i = 0; i < 3; i++) g_dmgPending[i] = -v[i] / len;     // towards the attacker
    g_dmgHavePending = true;
    if (g_dmgLogged < 6) {
        g_dmgLogged++;
        const float* p = (const float*)a[2];
        Log("Golpe recibido: direccion (%.2f %.2f %.2f) largo %.2f | a10 %s (%.0f %.0f %.0f) | dano %.1f", v[0], v[1], v[2], len,
            p && !IsBadReadPtr(p, 12) ? "vec" : "-", p && !IsBadReadPtr(p, 12) ? p[0] : 0, p && !IsBadReadPtr(p, 12) ? p[1] : 0,
            p && !IsBadReadPtr(p, 12) ? p[2] : 0, *(float*)&a[3]);
    }
}

static void __stdcall OnHit()
{
    g_hudHitTick = GetTickCount();
    GamepadRumble(180);
    if (!g_dmgHavePending) return;
    g_dmgHavePending = false;
    float hx = g_dmgPending[0], hz = g_dmgPending[2];
    if (hx * hx + hz * hz < 1e-4f) return;                            // straight from above/below
    int slot = 0;                                                       // reuse a close direction or the oldest
    for (int i = 0; i < 4; i++) {
        if (g_dmgDirs[i].tick && g_dmgDirs[i].x * hx + g_dmgDirs[i].z * hz > 0.94f) { slot = i; break; }
        if (g_dmgDirs[i].tick < g_dmgDirs[slot].tick) slot = i;
    }
    g_dmgDirs[slot] = { hx, hz, GetTickCount() };
}

static void DrawDamageDirs(float W, float H)
{
    const D3DMATRIX& v = g_mainView;
    const HudGlyph& g = g_gNamed[HN_ARC];
    float R = H * 0.2f, s = R / 280.0f;           // arc glyph: circle of radius 280 centred 300 px right of its left edge
    for (DamageDir& d : g_dmgDirs) {
        if (!d.tick) continue;
        float age = (GetTickCount() - d.tick) / 1000.0f;
        if (age > 2.0f) { d.tick = 0; continue; }
        float a = age < 0.8f ? 1 : 1 - (age - 0.8f) / 1.2f;
        float vx = d.x * v._11 + d.z * v._31, vz = d.x * v._13 + d.z * v._33;   // view space (y is up)
        float th = atan2f(vx, vz);                                              // 0 = ahead, + = right
        float cx = W * .5f + sinf(th) * 252 * s, cy = H * .5f - cosf(th) * 252 * s;
        PushQuadRot(cx, cy, g.w * s, g.h * s, th + 1.5707963f, g, Col(C_DANGER, 0.85f * a));
    }
}

static void Detour(DWORD addr, int len, void* before, bool passArgs)
{
    unsigned char* f = (unsigned char*)addr;
    unsigned char* t = (unsigned char*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    int n = 0;
    t[n++] = 0x60;                                                     // pushad
    if (passArgs) { t[n++] = 0x8D; t[n++] = 0x44; t[n++] = 0x24; t[n++] = 0x24; t[n++] = 0x50; }   // push &args
    t[n++] = 0xE8; *(DWORD*)(t + n) = (DWORD)before - (DWORD)(t + n + 4); n += 4;
    t[n++] = 0x61;                                                     // popad
    memcpy(t + n, f, len); n += len;
    t[n++] = 0xE9; *(DWORD*)(t + n) = (DWORD)(f + len) - (DWORD)(t + n + 4); n += 4;
    DWORD old;
    VirtualProtect(t, 64, PAGE_EXECUTE_READ, &old);                   // trampoline: executable, not writable
    VirtualProtect(f, len, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xE9;
    *(DWORD*)(f + 1) = (DWORD)t - (DWORD)(f + 5);
    for (int i = 5; i < len; i++) f[i] = 0x90;
    VirtualProtect(f, len, old, &old);
}

static void HookHudDraw()
{
    unsigned char* p = (unsigned char*)0x4c8b60;
    static const unsigned char orig[9] = { 0x55, 0x8b, 0xec, 0x81, 0xec, 0x4c, 0x05, 0x00, 0x00 };
    if (IsBadReadPtr(p, 9) || memcmp(p, orig, 9)) return;
    DWORD old;
    VirtualProtect(p, 9, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)HudDrawStub - (DWORD)(p + 5);
    memset(p + 5, 0x90, 4);
    VirtualProtect(p, 9, old, &old);
    static const unsigned char ammoOrig[9] = { 0x55, 0x8b, 0xec, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00 };
    static const unsigned char hitOrig[7] = { 0x55, 0x8b, 0xec, 0x51, 0x89, 0x4d, 0xfc };
    if (!memcmp((void*)0x4c8140, ammoOrig, 9)) Detour(0x4c8140, 9, (void*)OnAmmo, true);
    if (!memcmp((void*)0x4c8b40, hitOrig, 7)) Detour(0x4c8b40, 7, (void*)OnHit, false);
    static const unsigned char dmgOrig[6] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10 };
    if (!memcmp((void*)0x480d50, dmgOrig, 6)) Detour(0x480d50, 6, (void*)OnDamage, true);
}

// Upper case of a byte in the current language's code page (cp1252 / cp1250 / cp1251).
static unsigned char HudUpper(unsigned char c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    bool cyr = g_lang == LANG_RU || g_lang == LANG_UK;
    if (c >= 0xE0 && (cyr || (c <= 0xFE && c != 0xF7))) return c - 32;
    if (cyr) {
        static const unsigned char lo[] = { 0xB8, 0xB3, 0xBF, 0xBA, 0xB4 }, up[] = { 0xA8, 0xB2, 0xAF, 0xAA, 0xA5 };   // ё і ї є ґ
        for (int i = 0; i < 5; i++) if (c == lo[i]) return up[i];
    } else if (g_lang == LANG_PL) {
        static const unsigned char lo[] = { 0xB9, 0x9C, 0x9F, 0xB3, 0xBF }, up[] = { 0xA5, 0x8C, 0x8F, 0xA3, 0xAF };   // ą ś ź ł ż
        for (int i = 0; i < 5; i++) if (c == lo[i]) return up[i];
    }
    return c;
}

// Text hook helper: inside the HUD pass, keep the centre messages for our own drawing and drop
// every game text. Returns true when the game's text must not be drawn.
static bool HudPassText(DWORD* a)
{
    if (!g_inGameHud) return false;
    const char* s = (const char*)a[6];
    float y = *(float*)&a[2], x1 = *(float*)&a[1], x2 = *(float*)&a[3];
    bool centred = (a[7] & 1) && x2 - x1 > g_w * 0.5f;   // full-width centred line
    if (centred && s && !IsBadReadPtr(s, 1) && *s && g_hudMsgN < 4) {
        HudMsg& m = g_hudMsgs[g_hudMsgN++];
        strncpy_s(m.text, s, _TRUNCATE);
        for (unsigned char* c = (unsigned char*)m.text; *c; c++)
            *c = HudUpper(*c);
        m.y = y + 26 * g_k;
        m.a = ((a[5] >> 24) & 255) / 255.0f;
    }
    return true;
}

// The game's green crosshair (2D camera, a line list at the screen centre): hidden
// while the new HUD is on.
static bool IsGameCrosshair(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt, UINT start, UINT prims)
{
    if (!S.newHud || !S.enabled || g_inFX || g_drawsSince3D == 0 || prims > 16 || Is2D(d) || fabsf(g_curProjM._34) > 0.5f)
        return false;
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    D3DSURFACE_DESC td = {};
    if (t) {
        if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td);
        t->Release();
    }
    // 4 lines; texture 32x128 at rest, 128x128 while firing
    if (pt != D3DPT_LINELIST && (td.Width != 32 || td.Height != 128)) return false;
    float bb[4];
    if (!DrawBBox(d, (INT)start, 0, VertsForPrims(pt, prims), bb)) return false;
    static int logs;
    if (logs < 30 && IniF(L"Debug", L"HudPassLog", 0) != 0) {
        logs++;
        Log("mira: pt %d prims %u bbox %.0f,%.0f - %.0f,%.0f", (int)pt, prims, bb[0], bb[1], bb[2], bb[3]);
    }
    float cx = (bb[0] + bb[2]) * .5f, cy = (bb[1] + bb[3]) * .5f;
    // it opens up while firing (spread), so allow a wide box as long as it stays centred
    return fabsf(cx - g_w * .5f) < g_w * 0.03f && fabsf(cy - g_h * .5f) < g_h * 0.05f &&
           bb[2] - bb[0] < g_w * 0.35f && bb[3] - bb[1] < g_h * 0.45f;
}

// debug: DeadhuntFX_centro.txt -> log every non-3D draw near the screen centre for 4 frames
static int g_centerLog;
static void CenterLogFrame()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX_centro.txt", g_dir);
    if (g_centerLog > 0) g_centerLog--;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) { _wremove(path); g_centerLog = 4; }
}
static void CenterLogDraw(IDirect3DDevice9* d, const char* api, D3DPRIMITIVETYPE pt, UINT prims, const void* up, UINT st,
                          INT base, UINT mn, UINT nv, UINT start)
{
    if (!g_centerLog || g_inFX) return;
    bool ortho = fabsf(g_curProjM._34) < 0.5f, rhw = Is2D(d);
    if (!ortho && !rhw) return;
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    D3DSURFACE_DESC td = {};
    if (t) { if (t->GetType() == D3DRTYPE_TEXTURE) ((IDirect3DTexture9*)t)->GetLevelDesc(0, &td); t->Release(); }
    float bb[4] = { 0, 0, 0, 0 };
    UINT cnt = pt == D3DPT_POINTLIST ? prims : VertsForPrims(pt, prims);
    if (up) HudBBox((const unsigned char*)up + (size_t)mn * st, nv ? nv : cnt, st, rhw, bb);
    else if (!rhw) DrawBBox(d, nv ? base : (INT)start, mn, nv ? nv : cnt, bb);
    float cx = (bb[0] + bb[2]) * .5f, cy = (bb[1] + bb[3]) * .5f;
    if (fabsf(cx - g_w * .5f) > g_w * 0.1f || fabsf(cy - g_h * .5f) > g_h * 0.15f) return;
    Log("centro %s pt %d prims %u tex %ux%u %s bbox %.0f,%.0f-%.0f,%.0f inHUD %d d3d %d", api, (int)pt, prims, td.Width, td.Height,
        rhw ? "rhw" : "orto", bb[0], bb[1], bb[2], bb[3], (int)g_inGameHud, g_drawsSince3D);
}
