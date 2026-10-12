// DeadhuntFX - terrain and grass shading.
//
// The game draws the ground with the fixed-function pipeline (FVF 0x152: position, normal,
// diffuse, uv): stage0 = layer A, stage1 = layer B blended by the vertex alpha (BLENDCURRENTALPHA),
// stage2/3 = two lookup textures with camera-space texgen (ambient add + light multiply x4).
// Grass is FVF 0x112 quads from a 1024 atlas, alpha blended without depth writes, same light lookup.
//
// For those draws we keep the fixed-function vertex processing (lighting, fog, transforms stay
// exactly the game's) and bind our own pixel shader. Stage 4/5 texture coordinates are switched
// to camera-space position/normal with the inverse view as texture transform, so the shader
// receives the world position and normal. Per ground texture a height+normal map is generated
// from its luminance when it is first drawn.
//
// [Terrain] in DeadhuntFX.ini: Enabled, Parallax (depth in world units), Relief, AntiTile,
// HeightBlend, FadeDistance, Grass, GrassAO, GrassVariation, GrassBacklight.
// Included from main.cpp after shadow_map.inl.

static struct {
    bool enabled, grass, objects;
    float objRelief, parallax, relief, antiTile, heightBlend, fadeDist, grassAO, grassVar, grassBack;
} T;

static void TerrainLoadSettings()
{
    T.enabled     = IniF(L"Terrain", L"Enabled", 1) != 0;
    T.parallax    = IniF(L"Terrain", L"Parallax", 9.0f);
    T.relief      = IniF(L"Terrain", L"Relief", 0.6f);
    T.antiTile    = IniF(L"Terrain", L"AntiTile", 1.0f);
    T.heightBlend = IniF(L"Terrain", L"HeightBlend", 1.0f);
    T.fadeDist    = IniF(L"Terrain", L"FadeDistance", 2500.0f);
    T.grass       = IniF(L"Terrain", L"Grass", 1) != 0;
    T.grassAO     = IniF(L"Terrain", L"GrassAO", 0.6f);
    T.grassVar    = IniF(L"Terrain", L"GrassVariation", 0.6f);
    T.grassBack   = IniF(L"Terrain", L"GrassBacklight", 0.5f);
    T.objects     = IniF(L"Terrain", L"Objects", 1) != 0;
    T.objRelief   = IniF(L"Terrain", L"ObjectRelief", 0.8f);
}

static const char* kTerrainPS =
    "sampler2D sA:register(s0); sampler2D sB:register(s1); sampler2D sAdd:register(s2); sampler2D sMul:register(s3);\n"
    "sampler2D hA:register(s4); sampler2D hB:register(s5); sampler2D sN:register(s6);\n"
    "float4 cCam:register(c0); float4 cSun:register(c1); float4 cP:register(c2);\n"
    "float4 cT:register(c4); float4 cB:register(c5); float4 cQ:register(c6);\n"
    "struct I { float2 uv:TEXCOORD0; float3 l2:TEXCOORD2; float3 l3:TEXCOORD3; float3 wp:TEXCOORD4; float3 n:TEXCOORD5; float4 col:COLOR0; };\n"
    "float Hgt(float2 uv, float2 dx, float2 dy, float a) {\n"
    "  return lerp(tex2Dgrad(hB, uv, dx, dy).a, tex2Dgrad(hA, uv, dx, dy).a, a); }\n"
    "float4 main(I i) : COLOR {\n"
    "  float3 V = cCam.xyz - i.wp; float dist = length(V); V /= dist;\n"
    "  float3 N = normalize(i.n);\n"
    "  float3 Tn = normalize(cT.xyz - N * dot(cT.xyz, N)); float3 Bn = normalize(cB.xyz - N * dot(cB.xyz, N));\n"
    "  float3 vt = float3(dot(V, Tn), dot(V, Bn), dot(V, N));\n"
    "  float fade = saturate(1 - dist * cP.w);\n"
    "  float2 dir = vt.xy / max(vt.z, 0.2) * cP.x * fade;\n"
    "  float2 dx = ddx(i.uv), dy = ddy(i.uv);\n"
    "  float a = i.col.a;\n"
    // linear search: the deepest layer still above the surface, then a binary refine
    "  float lo = 0, hit = 0;\n"
    "  [unroll] for (int k = 1; k <= 10; k++) {\n"
    "    float d = k * 0.1; float h = Hgt(i.uv - dir * d, dx, dy, a);\n"
    "    float below = step(1 - h, d); lo = lerp(d, lo, max(hit, below)); hit = max(hit, below); }\n"
    "  float hi = lo + 0.1;\n"
    "  [unroll] for (int r = 0; r < 4; r++) {\n"
    "    float m = (lo + hi) * 0.5; float b = step(1 - Hgt(i.uv - dir * m, dx, dy, a), m);\n"
    "    hi = lerp(hi, m, b); lo = lerp(m, lo, b); }\n"
    "  float2 uv = i.uv - dir * (lo + hi) * 0.5;\n"
    // layers, anti-tiling: a rotated second sample shown where the low-frequency noise says so
    "  float2 nz = tex2D(sN, i.wp.xz * (1.0 / 6000.0)).rg;\n"
    "  float m2 = smoothstep(0.4, 0.6, nz.r) * cQ.x;\n"
    "  float2 uv2 = float2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8) * 1.13 + 0.37;\n"
    "  float2 dx2 = float2(dx.x * 0.8 - dx.y * 0.6, dx.x * 0.6 + dx.y * 0.8) * 1.13;\n"
    "  float2 dy2 = float2(dy.x * 0.8 - dy.y * 0.6, dy.x * 0.6 + dy.y * 0.8) * 1.13;\n"
    "  float4 nA = lerp(tex2Dgrad(hA, uv, dx, dy), tex2Dgrad(hA, uv2, dx2, dy2), m2);\n"
    "  float4 nB = lerp(tex2Dgrad(hB, uv, dx, dy), tex2Dgrad(hB, uv2, dx2, dy2), m2);\n"
    "  float3 cA = lerp(tex2Dgrad(sA, uv, dx, dy).rgb, tex2Dgrad(sA, uv2, dx2, dy2).rgb, m2);\n"
    "  float3 cBl = lerp(tex2Dgrad(sB, uv, dx, dy).rgb, tex2Dgrad(sB, uv2, dx2, dy2).rgb, m2);\n"
    // height blend between the layers (only where the vertex alpha is in between)
    "  float ha = nA.a + a, hb = nB.a + 1 - a; float top = max(ha, hb) - 0.25;\n"
    "  float wa = max(ha - top, 0), wb = max(hb - top, 0);\n"
    "  float ab = lerp(a, wa / (wa + wb + 1e-4), cQ.y);\n"
    "  float3 col = lerp(cBl, cA, ab);\n"
    "  float4 nm = lerp(nB, nA, ab);\n"
    "  col *= lerp(1.0, lerp(0.88, 1.08, nz.g), cQ.x);\n"
    // relief lighting from the generated normals, relative to the flat surface
    "  float3 tn = float3(nm.r * 2 - 1, nm.g * 2 - 1, 0); tn.z = sqrt(saturate(1 - dot(tn.xy, tn.xy)));\n"
    "  float3 Nw = normalize(Tn * tn.x + Bn * tn.y + N * tn.z);\n"
    "  float shade = 1 + cP.y * fade * (dot(Nw, cSun.xyz) - dot(N, cSun.xyz)) * 1.6;\n"
    "  shade *= lerp(1.0, 0.82 + 0.3 * nm.a, cP.y * fade);\n"
    "  col *= clamp(shade, 0.45, 1.6);\n"
    // the game's own stages 2 and 3
    "  float4 lAdd = tex2D(sAdd, i.l2.xy / i.l2.z); float4 lMul = tex2D(sMul, i.l3.xy / i.l3.z);\n"
    "  col = lAdd.rgb + (1 - lAdd.a) * col;\n"
    "  col = col * lMul.rgb * 4;\n"
    "  return float4(col, 1);\n"
    "}\n";

static const char* kGrassPS =
    "sampler2D sA:register(s0); sampler2D sMul:register(s1); sampler2D sN:register(s6);\n"
    "float4 cCam:register(c0); float4 cSun:register(c1); float4 cG:register(c2);\n"
    "struct I { float2 uv:TEXCOORD0; float3 l1:TEXCOORD1; float3 wp:TEXCOORD4; float4 col:COLOR0; };\n"
    "float4 main(I i) : COLOR {\n"
    "  float4 c = tex2D(sA, i.uv);\n"
    "  float u = i.uv.x, v = i.uv.y;\n"
    // grass cells of the atlas (top = 0, base = 1): big clump, thin strip, small tuft
    "  float big   = step(u, 0.27) * step(v, 0.145);\n"
    "  float strip = step(0.28, u) * step(u, 0.42) * step(v, 0.08);\n"
    "  float tuft  = step(0.27, u) * step(u, 0.33) * step(0.085, v) * step(v, 0.152);\n"
    "  float g = saturate(big + strip + tuft);\n"
    "  float t = big * saturate((v - 0.015) / 0.124) + strip * saturate((v - 0.017) / 0.06) + tuft * saturate((v - 0.088) / 0.061);\n"
    "  float3 rgb = c.rgb;\n"
    "  float2 nz = tex2D(sN, i.wp.xz * (1.0 / 2500.0)).rg;\n"
    "  float dry = smoothstep(0.5, 0.85, nz.r) * cG.y * g;\n"
    "  rgb = lerp(rgb, rgb * float3(1.25, 1.12, 0.7), dry);\n"
    "  rgb *= lerp(1.0, lerp(0.85, 1.1, nz.g), cG.y * g);\n"
    "  rgb *= 1 - cG.x * g * pow(t, 1.5) * 0.6;\n"
    "  float3 V = normalize(i.wp - cCam.xyz);\n"
    "  float back = pow(saturate(dot(V, cSun.xyz)), 4) * (1 - t) * cG.z * g;\n"
    "  rgb += rgb * float3(0.45, 0.55, 0.15) * back;\n"
    "  float4 lMul = tex2D(sMul, i.l1.xy / i.l1.z);\n"
    "  return float4(rgb * lMul.rgb * 4, c.a * i.col.a);\n"
    "}\n";

// Lit scene objects (tree trunks, rocks, walls, ruins): texture x diffuse (x2), relief lighting
// from the generated normal map; the tangent frame comes from screen derivatives (any uv mapping).
static const char* kObjectPS =
    "sampler2D sA:register(s0); sampler2D hA:register(s4);\n"
    "float4 cCam:register(c0); float4 cSun:register(c1); float4 cP:register(c2); float4 cA:register(c3);\n"
    "struct I { float2 uv:TEXCOORD0; float3 wp:TEXCOORD4; float3 n:TEXCOORD5; float4 col:COLOR0; };\n"
    "float4 main(I i) : COLOR {\n"
    "  float4 t = tex2D(sA, i.uv); float4 hn = tex2D(hA, i.uv);\n"
    "  float3 col = t.rgb * lerp(1, i.col.rgb, cP.z) * cP.y;\n"
    "  float a = t.a * lerp(1, i.col.a, cA.x) * cA.y;\n"
    "  float3 N = normalize(i.n);\n"
    "  float3 dp1 = ddx(i.wp), dp2 = ddy(i.wp); float2 du1 = ddx(i.uv), du2 = ddy(i.uv);\n"
    "  float3 p2 = cross(dp2, N), p1 = cross(N, dp1);\n"
    "  float3 Tn = p2 * du1.x + p1 * du2.x, Bn = p2 * du1.y + p1 * du2.y;\n"
    "  float inv = rsqrt(max(max(dot(Tn, Tn), dot(Bn, Bn)), 1e-24)); Tn *= inv; Bn *= inv;\n"
    "  float3 tn = float3(hn.r * 2 - 1, hn.g * 2 - 1, 0); tn.z = sqrt(saturate(1 - dot(tn.xy, tn.xy)));\n"
    "  float3 Nw = normalize(Tn * tn.x + Bn * tn.y + N * tn.z);\n"
    "  float fade = saturate(1 - length(cCam.xyz - i.wp) * cP.w);\n"
    "  float shade = 1 + cP.x * fade * (dot(Nw, cSun.xyz) - dot(N, cSun.xyz)) * 1.6;\n"
    "  shade *= lerp(1.0, 0.8 + 0.32 * hn.a, cP.x * fade);\n"
    "  return float4(col * clamp(shade, 0.45, 1.6), a);\n"
    "}\n";

static IDirect3DPixelShader9 *g_terrPS, *g_grassPS, *g_objPS;
static bool g_terrTried;
static IDirect3DTexture9* g_noiseTex;

struct HeightEntry { IDirect3DBaseTexture9* src; IDirect3DTexture9* hn; };
static HeightEntry g_heights[256];
static int g_heightCount;

static bool TerrainShaders(IDirect3DDevice9* d)
{
    if (g_terrTried) return g_terrPS != nullptr;
    g_terrTried = true;
    if (!g_D3DCompile) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) g_D3DCompile = (PFN_FXCompile)GetProcAddress(m, "D3DCompile");
    }
    if (!g_D3DCompile) return false;
    const char* srcs[3] = { kTerrainPS, kGrassPS, kObjectPS };
    IDirect3DPixelShader9** outs[3] = { &g_terrPS, &g_grassPS, &g_objPS };
    static const char* names[3] = { "suelo", "hierba", "objetos" };
    for (int i = 0; i < 3; i++) {
        ID3DBlob *code = nullptr, *err = nullptr;
        if (FAILED(g_D3DCompile(srcs[i], strlen(srcs[i]), names[i], nullptr, nullptr, "main", "ps_2_a",
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err)) ||
            FAILED(d->CreatePixelShader((const DWORD*)code->GetBufferPointer(), outs[i]))) {
            Log("Terreno: shader %s no compila: %s", names[i], err ? (const char*)err->GetBufferPointer() : "?");
            *outs[i] = nullptr;
        }
        if (code) code->Release();
        if (err) err->Release();
    }
    Log("Terreno: shaders %s / %s / %s", g_terrPS ? "suelo ok" : "suelo NO", g_grassPS ? "hierba ok" : "hierba NO",
        g_objPS ? "objetos ok" : "objetos NO");
    return g_terrPS != nullptr;
}

// Box-filtered mip chain for an A8R8G8B8 texture whose level 0 is already filled.
static void FillMips(IDirect3DTexture9* t)
{
    DWORD levels = t->GetLevelCount();
    for (DWORD l = 1; l < levels; l++) {
        D3DLOCKED_RECT a, b;
        D3DSURFACE_DESC da, db;
        t->GetLevelDesc(l - 1, &da);
        t->GetLevelDesc(l, &db);
        if (FAILED(t->LockRect(l - 1, &a, nullptr, D3DLOCK_READONLY))) return;
        if (FAILED(t->LockRect(l, &b, nullptr, 0))) { t->UnlockRect(l - 1); return; }
        for (UINT y = 0; y < db.Height; y++)
            for (UINT x = 0; x < db.Width; x++)
                for (int c = 0; c < 4; c++) {
                    UINT sx = x * 2 < da.Width ? x * 2 : da.Width - 1, sy = y * 2 < da.Height ? y * 2 : da.Height - 1;
                    UINT sx1 = sx + 1 < da.Width ? sx + 1 : sx, sy1 = sy + 1 < da.Height ? sy + 1 : sy;
                    const unsigned char* p = (const unsigned char*)a.pBits;
                    int s = p[sy * a.Pitch + sx * 4 + c] + p[sy * a.Pitch + sx1 * 4 + c] +
                            p[sy1 * a.Pitch + sx * 4 + c] + p[sy1 * a.Pitch + sx1 * 4 + c];
                    ((unsigned char*)b.pBits)[y * b.Pitch + x * 4 + c] = (unsigned char)((s + 2) / 4);
                }
        t->UnlockRect(l);
        t->UnlockRect(l - 1);
    }
}

// Wrapping box blur along x then y (tileable textures stay tileable).
static void BlurWrap(float* img, int w, int h, int r)
{
    float* tmp = (float*)malloc(sizeof(float) * (size_t)(w > h ? w : h));
    if (!tmp) return;
    for (int y = 0; y < h; y++) {
        float* row = img + (size_t)y * w;
        float s = 0;
        for (int k = -r; k <= r; k++) s += row[((k % w) + w) % w];
        for (int x = 0; x < w; x++) {
            tmp[x] = s / (2 * r + 1);
            s += row[(x + r + 1) % w] - row[((x - r) % w + w) % w];
        }
        memcpy(row, tmp, sizeof(float) * w);
    }
    for (int x = 0; x < w; x++) {
        float s = 0;
        for (int k = -r; k <= r; k++) s += img[(size_t)(((k % h) + h) % h) * w + x];
        for (int y = 0; y < h; y++) {
            tmp[y] = s / (2 * r + 1);
            s += img[(size_t)((y + r + 1) % h) * w + x] - img[(size_t)(((y - r) % h + h) % h) * w + x];
        }
        for (int y = 0; y < h; y++) img[(size_t)y * w + x] = tmp[y];
    }
    free(tmp);
}

// Height (A) and tangent-space normal (RG) from the luminance of a ground texture.
static IDirect3DTexture9* BuildHeightMap(IDirect3DDevice9* d, IDirect3DBaseTexture9* bt)
{
    if (!bt || bt->GetType() != D3DRTYPE_TEXTURE) return nullptr;
    IDirect3DTexture9* src = (IDirect3DTexture9*)bt;
    D3DSURFACE_DESC desc;
    if (FAILED(src->GetLevelDesc(0, &desc)) || desc.Pool == D3DPOOL_DEFAULT ||
        (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A1R5G5B5 &&
         desc.Format != D3DFMT_X1R5G5B5 && desc.Format != D3DFMT_R5G6B5 && desc.Format != D3DFMT_A4R4G4B4) ||
        desc.Width > 2048 || desc.Height > 2048 || desc.Width < 32 || desc.Height < 32)
        return nullptr;
    int w = (int)desc.Width, h = (int)desc.Height;
    float* L = (float*)malloc(sizeof(float) * w * h);
    float* B = (float*)malloc(sizeof(float) * w * h);
    if (!L || !B) { free(L); free(B); return nullptr; }
    D3DLOCKED_RECT lr;
    if (FAILED(src->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) { free(L); free(B); return nullptr; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* row = (const unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
            float r, g, b;
            if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) {
                const unsigned char* p = row + x * 4;
                r = p[2] / 255.0f; g = p[1] / 255.0f; b = p[0] / 255.0f;
            } else {
                unsigned v = ((const unsigned short*)row)[x];
                if (desc.Format == D3DFMT_R5G6B5) { r = (v >> 11) / 31.0f; g = ((v >> 5) & 63) / 63.0f; b = (v & 31) / 31.0f; }
                else if (desc.Format == D3DFMT_A4R4G4B4) { r = ((v >> 8) & 15) / 15.0f; g = ((v >> 4) & 15) / 15.0f; b = (v & 15) / 15.0f; }
                else { r = ((v >> 10) & 31) / 31.0f; g = ((v >> 5) & 31) / 31.0f; b = (v & 31) / 31.0f; }
            }
            L[y * w + x] = r * 0.30f + g * 0.55f + b * 0.15f;
        }
    src->UnlockRect(0);
    // high pass: remove the large-scale brightness (painted light, color patches)
    memcpy(B, L, sizeof(float) * w * h);
    for (int i = 0; i < 3; i++) BlurWrap(B, w, h, w / 24 > 2 ? w / 24 : 2);
    for (int i = 0; i < w * h; i++) L[i] = L[i] - B[i];
    BlurWrap(L, w, h, 1);
    // normalize with percentiles
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < w * h; i++) { lo = fminf(lo, L[i]); hi = fmaxf(hi, L[i]); }
    int hist[256] = {};
    for (int i = 0; i < w * h; i++) hist[(int)((L[i] - lo) / (hi - lo + 1e-6f) * 255.0f)]++;
    int acc = 0, p2 = 0, p98 = 255;
    for (int b = 0; b < 256; b++) { acc += hist[b]; if (acc < w * h / 50) p2 = b; if (acc < w * h * 49 / 50) p98 = b + 1; }
    float a0 = lo + (hi - lo) * p2 / 255.0f, a1 = lo + (hi - lo) * p98 / 255.0f;
    for (int i = 0; i < w * h; i++) L[i] = fminf(fmaxf((L[i] - a0) / (a1 - a0 + 1e-6f), 0.0f), 1.0f);

    IDirect3DTexture9* out = nullptr;
    g_inFX = true;
    HRESULT hr = d->CreateTexture(w, h, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &out, nullptr);
    g_inFX = false;
    if (FAILED(hr) || !out) { free(L); free(B); return nullptr; }
    if (SUCCEEDED(out->LockRect(0, &lr, nullptr, 0))) {
        const float strength = w / 64.0f;     // slope of a full 0..1 height step over one pixel
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float dx = (L[y * w + (x + 1) % w] - L[y * w + (x + w - 1) % w]) * 0.5f * strength * 0.12f;
                float dy = (L[((y + 1) % h) * w + x] - L[((y + h - 1) % h) * w + x]) * 0.5f * strength * 0.12f;
                float nx = -dx, ny = -dy, nz = 1;
                float len = sqrtf(nx * nx + ny * ny + nz * nz);
                unsigned char* p = (unsigned char*)lr.pBits + (size_t)y * lr.Pitch + x * 4;
                p[2] = (unsigned char)((nx / len * 0.5f + 0.5f) * 255.0f + 0.5f);
                p[1] = (unsigned char)((ny / len * 0.5f + 0.5f) * 255.0f + 0.5f);
                p[0] = (unsigned char)(nz / len * 255.0f + 0.5f);
                p[3] = (unsigned char)(L[y * w + x] * 255.0f + 0.5f);
            }
        out->UnlockRect(0);
        FillMips(out);
    }
    free(L);
    free(B);
    Log("Terreno: mapa de relieve generado para la textura %p (%dx%d)", (void*)bt, w, h);
    return out;
}

static IDirect3DTexture9* HeightFor(IDirect3DDevice9* d, IDirect3DBaseTexture9* t)
{
    for (int i = 0; i < g_heightCount; i++) if (g_heights[i].src == t) return g_heights[i].hn;
    if (g_heightCount >= 256) return nullptr;
    IDirect3DTexture9* hn = BuildHeightMap(d, t);
    g_heights[g_heightCount++] = { t, hn };
    return hn;
}

static void ClearHeights()
{
    for (int i = 0; i < g_heightCount; i++) if (g_heights[i].hn) g_heights[i].hn->Release();
    g_heightCount = 0;
}

// Tileable smooth value noise: R = coarse, G = finer.
static void BuildNoise(IDirect3DDevice9* d)
{
    if (g_noiseTex) return;
    const int N = 256;
    g_inFX = true;
    HRESULT hr = d->CreateTexture(N, N, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_noiseTex, nullptr);
    g_inFX = false;
    if (FAILED(hr)) { g_noiseTex = nullptr; return; }
    auto hash = [](int x, int y, int s) {
        unsigned h = (unsigned)(x * 374761393 + y * 668265263 + s * 2147483647u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
    };
    auto value = [&](float x, float y, int period, int s) {
        int xi = (int)floorf(x), yi = (int)floorf(y);
        float fx = x - xi, fy = y - yi;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
        auto g = [&](int a, int b) { return hash(((a % period) + period) % period, ((b % period) + period) % period, s); };
        return (g(xi, yi) * (1 - fx) + g(xi + 1, yi) * fx) * (1 - fy) + (g(xi, yi + 1) * (1 - fx) + g(xi + 1, yi + 1) * fx) * fy;
    };
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(g_noiseTex->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float u = x / (float)N, v = y / (float)N;
                float r = value(u * 4, v * 4, 4, 1) * 0.65f + value(u * 8, v * 8, 8, 2) * 0.35f;
                float g = value(u * 6, v * 6, 6, 3) * 0.5f + value(u * 16, v * 16, 16, 4) * 0.5f;
                unsigned char* p = (unsigned char*)lr.pBits + (size_t)y * lr.Pitch + x * 4;
                p[2] = (unsigned char)(r * 255); p[1] = (unsigned char)(g * 255); p[0] = 0; p[3] = 255;
            }
        g_noiseTex->UnlockRect(0);
        FillMips(g_noiseTex);
    }
}

// ---------------------------------------------------------------- per draw

enum { TD_NONE = 0, TD_GROUND = 1, TD_GRASS = 2, TD_OBJECT = 3 };
static int g_tdKind;
static struct {
    DWORD tci[8], ttf[8];
    D3DMATRIX tm[8];
    IDirect3DBaseTexture9* tex[3];
    DWORD samp[7][4];
} g_tdSave;

static const D3DSAMPLERSTATETYPE kSampStates[4] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAXANISOTROPY };

// Lit objects: position + normal + uv, stage0 = texture x diffuse (MODULATE / MODULATE2X), stage1 off.
static DWORD g_objOp, g_objAop;
static int ClassifyObject(IDirect3DDevice9* d, DWORD fvf)
{
    if (!T.objects || !g_objPS || g_depthDone) return TD_NONE;          // g_depthDone: the weapon pass
    if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ || !(fvf & D3DFVF_NORMAL) || (fvf & D3DFVF_TEXCOUNT_MASK) == 0)
        return TD_NONE;
    DWORD op0, a1, a2, op1, aop0, aa1, aa2;
    d->GetTextureStageState(0, D3DTSS_COLOROP, &op0);
    d->GetTextureStageState(0, D3DTSS_COLORARG1, &a1);
    d->GetTextureStageState(0, D3DTSS_COLORARG2, &a2);
    d->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop0);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG1, &aa1);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG2, &aa2);
    d->GetTextureStageState(1, D3DTSS_COLOROP, &op1);
    DWORD tci0;
    d->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &tci0);
    if (op1 != D3DTOP_DISABLE || tci0 != 0) return TD_NONE;
    if (!((op0 == D3DTOP_MODULATE || op0 == D3DTOP_MODULATE2X) && a1 == D3DTA_TEXTURE &&
          (a2 == D3DTA_DIFFUSE || a2 == D3DTA_CURRENT)))
        return TD_NONE;
    if (!(aop0 == D3DTOP_SELECTARG1 || ((aop0 == D3DTOP_MODULATE || aop0 == D3DTOP_MODULATE2X) &&
                                         aa1 == D3DTA_TEXTURE && (aa2 == D3DTA_DIFFUSE || aa2 == D3DTA_CURRENT)))
        || (aop0 == D3DTOP_SELECTARG1 && aa1 != D3DTA_TEXTURE))
        return TD_NONE;
    g_objOp = op0;
    g_objAop = aop0;
    return TD_OBJECT;
}

static int ClassifyTerrain(IDirect3DDevice9* d)
{
    if (!T.enabled || g_inFX || g_fxDone) return TD_NONE;
    DWORD fvf = 0;
    d->GetFVF(&fvf);
    if (fvf != 0x152 && fvf != 0x112) return ClassifyObject(d, fvf);
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    d->GetVertexShader(&vs);
    d->GetPixelShader(&ps);
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (vs || ps) return TD_NONE;
    DWORD op0, op1, aop0, ab, zw;
    d->GetTextureStageState(0, D3DTSS_COLOROP, &op0);
    d->GetTextureStageState(1, D3DTSS_COLOROP, &op1);
    d->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop0);
    if (fvf == 0x152) {
        DWORD op2, op3;
        d->GetTextureStageState(2, D3DTSS_COLOROP, &op2);
        d->GetTextureStageState(3, D3DTSS_COLOROP, &op3);
        return op0 == D3DTOP_SELECTARG1 && op1 == D3DTOP_BLENDCURRENTALPHA && op2 == D3DTOP_MODULATEINVALPHA_ADDCOLOR &&
               op3 == D3DTOP_MODULATE4X ? TD_GROUND : TD_NONE;
    }
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    if (op0 != D3DTOP_SELECTARG1 || aop0 != D3DTOP_MODULATE || op1 != D3DTOP_MODULATE4X || !ab || zw)
        return ClassifyObject(d, fvf);
    if (!T.grass || !g_grassPS) return TD_NONE;
    IDirect3DBaseTexture9* t = nullptr;
    d->GetTexture(0, &t);
    bool atlas = false;
    if (t) {
        D3DSURFACE_DESC desc;
        atlas = t->GetType() == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc)) &&
                desc.Width == 1024 && desc.Height == 1024;
        t->Release();
    }
    return atlas ? TD_GRASS : TD_NONE;
}

static void SetCoordStage(IDirect3DDevice9* d, DWORD s, DWORD gen, const D3DMATRIX& m)
{
    d->GetTextureStageState(s, D3DTSS_TEXCOORDINDEX, &g_tdSave.tci[s]);
    d->GetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, &g_tdSave.ttf[s]);
    d->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &g_tdSave.tm[s]);
    d->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, gen | s);
    d->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
    oSetTransform(d, (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &m);
}

static void SaveSampler(IDirect3DDevice9* d, DWORD s)
{
    for (int k = 0; k < 4; k++) d->GetSamplerState(s, kSampStates[k], &g_tdSave.samp[s][k]);
}

static void SetSampler(IDirect3DDevice9* d, DWORD s, bool aniso)
{
    SaveSampler(d, s);
    d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    d->SetSamplerState(s, D3DSAMP_MINFILTER, aniso ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
    d->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, aniso ? 16 : 1);
}

static void RestoreSampler(IDirect3DDevice9* d, DWORD s)
{
    for (int k = 0; k < 4; k++) d->SetSamplerState(s, kSampStates[k], g_tdSave.samp[s][k]);
}

// Called right before the game's draw; returns true when our shader is bound (call TerrainEnd after).
static bool TerrainBegin(IDirect3DDevice9* d)
{
    g_tdKind = TD_NONE;
    if (!T.enabled || g_inFX || g_fxDone) return false;
    if (!TerrainShaders(d)) return false;
    int kind = ClassifyTerrain(d);
    if (!kind) return false;

    D3DMATRIX view;
    d->GetTransform(D3DTS_VIEW, &view);
    M4 inv;
    if (!Inverse(ToM4(view), inv)) return false;
    D3DMATRIX invView, invRot;
    memcpy(invView.m, inv.m, sizeof(invView.m));
    invRot = invView;
    invRot._41 = invRot._42 = invRot._43 = 0;
    float cam[4] = { inv.m[3][0], inv.m[3][1], inv.m[3][2], 1 };
    float sun[4] = { 0, 1, 0, 0 };
    if (g_haveSun) { sun[0] = -g_sunDir[0]; sun[1] = -g_sunDir[1]; sun[2] = -g_sunDir[2]; }

    BuildNoise(d);
    if (kind == TD_OBJECT) {
        IDirect3DBaseTexture9* t0 = nullptr;
        d->GetTexture(0, &t0);
        IDirect3DTexture9* h0 = t0 ? HeightFor(d, t0) : nullptr;
        if (t0) t0->Release();
        if (!h0) return false;
        d->GetTexture(4, &g_tdSave.tex[0]);
        g_tdSave.tex[1] = g_tdSave.tex[2] = nullptr;
        SetCoordStage(d, 4, D3DTSS_TCI_CAMERASPACEPOSITION, invView);
        SetCoordStage(d, 5, D3DTSS_TCI_CAMERASPACENORMAL, invRot);
        d->SetTexture(4, h0);
        SaveSampler(d, 4);
        DWORD au, av, mf;
        d->GetSamplerState(0, D3DSAMP_ADDRESSU, &au);
        d->GetSamplerState(0, D3DSAMP_ADDRESSV, &av);
        d->GetSamplerState(0, D3DSAMP_MINFILTER, &mf);
        d->SetSamplerState(4, D3DSAMP_ADDRESSU, au);                      // same addressing as the texture
        d->SetSamplerState(4, D3DSAMP_ADDRESSV, av);
        d->SetSamplerState(4, D3DSAMP_MINFILTER, mf);
        float p[4] = { T.objRelief, g_objOp == D3DTOP_MODULATE2X ? 2.0f : 1.0f, 1, T.fadeDist > 1 ? 1.0f / T.fadeDist : 0 };
        float a[4] = { g_objAop == D3DTOP_SELECTARG1 ? 0.0f : 1.0f, g_objAop == D3DTOP_MODULATE2X ? 2.0f : 1.0f, 0, 0 };
        d->SetPixelShaderConstantF(0, cam, 1);
        d->SetPixelShaderConstantF(1, sun, 1);
        d->SetPixelShaderConstantF(2, p, 1);
        d->SetPixelShaderConstantF(3, a, 1);
        d->SetPixelShader(g_objPS);
    } else if (kind == TD_GROUND) {
        IDirect3DBaseTexture9 *t0 = nullptr, *t1 = nullptr;
        d->GetTexture(0, &t0);
        d->GetTexture(1, &t1);
        IDirect3DTexture9* h0 = HeightFor(d, t0);
        IDirect3DTexture9* h1 = HeightFor(d, t1);
        if (t0) t0->Release();
        if (t1) t1->Release();
        if (!h0 || !h1 || !g_noiseTex) return false;

        for (int s = 4; s <= 6; s++) d->GetTexture(s, &g_tdSave.tex[s - 4]);
        SetCoordStage(d, 4, D3DTSS_TCI_CAMERASPACEPOSITION, invView);   // stages 2/3 keep the game's lookup coords
        SetCoordStage(d, 5, D3DTSS_TCI_CAMERASPACENORMAL, invRot);
        d->SetTexture(4, h0);
        d->SetTexture(5, h1);
        d->SetTexture(6, g_noiseTex);
        SetSampler(d, 0, true);
        SetSampler(d, 1, true);
        SetSampler(d, 4, true);
        SetSampler(d, 5, true);
        SetSampler(d, 6, false);
        const float uvPerUnit = 1.0f / 500.0f;      // the ground is mapped u = x/500, v = -z/500
        float p[4] = { T.parallax * uvPerUnit, T.relief, 0, T.fadeDist > 1 ? 1.0f / T.fadeDist : 0 };
        float tv[4] = { 1, 0, 0, 0 }, bv[4] = { 0, 0, -1, 0 };
        float q[4] = { T.antiTile, T.heightBlend, 0, 0 };
        d->SetPixelShaderConstantF(0, cam, 1);
        d->SetPixelShaderConstantF(1, sun, 1);
        d->SetPixelShaderConstantF(2, p, 1);
        d->SetPixelShaderConstantF(4, tv, 1);
        d->SetPixelShaderConstantF(5, bv, 1);
        d->SetPixelShaderConstantF(6, q, 1);
        d->SetPixelShader(g_terrPS);
    } else {
        if (!g_noiseTex) return false;
        d->GetTexture(6, &g_tdSave.tex[2]);
        g_tdSave.tex[0] = g_tdSave.tex[1] = nullptr;
        SetCoordStage(d, 4, D3DTSS_TCI_CAMERASPACEPOSITION, invView);
        d->SetTexture(6, g_noiseTex);
        SetSampler(d, 6, false);
        float g[4] = { T.grassAO, T.grassVar, T.grassBack, 0 };
        d->SetPixelShaderConstantF(0, cam, 1);
        d->SetPixelShaderConstantF(1, sun, 1);
        d->SetPixelShaderConstantF(2, g, 1);
        d->SetPixelShader(g_grassPS);
    }
    g_tdKind = kind;
    return true;
}

static void TerrainEnd(IDirect3DDevice9* d)
{
    if (!g_tdKind) return;
    d->SetPixelShader(nullptr);
    int last = g_tdKind == TD_GRASS ? 4 : 5;
    for (int s = 4; s <= last; s++) {
        d->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, g_tdSave.tci[s]);
        d->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, g_tdSave.ttf[s]);
        oSetTransform(d, (D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &g_tdSave.tm[s]);
    }
    if (g_tdKind == TD_OBJECT) {
        RestoreSampler(d, 4);
        d->SetTexture(4, g_tdSave.tex[0]);
        if (g_tdSave.tex[0]) g_tdSave.tex[0]->Release();
        g_tdSave.tex[0] = nullptr;
        g_tdKind = TD_NONE;
        return;
    }
    if (g_tdKind == TD_GROUND) {
        RestoreSampler(d, 0); RestoreSampler(d, 1); RestoreSampler(d, 4); RestoreSampler(d, 5);
    }
    RestoreSampler(d, 6);
    for (int s = 4; s <= 6; s++) {
        if (g_tdKind == TD_GRASS && s < 6) continue;
        d->SetTexture(s, g_tdSave.tex[s - 4]);
        if (g_tdSave.tex[s - 4]) g_tdSave.tex[s - 4]->Release();
        g_tdSave.tex[s - 4] = nullptr;
    }
    g_tdKind = TD_NONE;
}

// Once per frame: height maps are rebuilt after a menu or loading screen (texture pointers can be reused).
static void TerrainFrame(bool had3D)
{
    static int menuFrames;
    menuFrames = had3D ? 0 : menuFrames + 1;
    if (menuFrames == 30) ClearHeights();
}
