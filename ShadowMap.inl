// DeadhuntFX - sun shadow maps.
//
// World draws (before the game clears depth for the weapon) are recorded and replayed
// from the sun into an INTZ depth texture. Every game vertex shader is classified from
// its bytecode: we find "m4x4 oPos, src, cN" to know which constants hold the camera
// matrix, and replace them with the sun camera on replay:
//   VS_WVP   src is a vertex input -> cN..cN+3 = world*view*proj  (LandObjects c4, DotBump c9)
//   VS_CAM   skinned (blendweight)  -> cN..cN+3 = projection, positions in camera space (Skin* c2)
// Draws without a vertex shader use the fixed-function WORLD/VIEW/PROJ transforms.
// Included from DeadhuntFX.cpp after the hooks section.

// ---------------------------------------------------------------- small matrix helpers

struct M4 { float m[4][4]; };

static M4 ToM4(const D3DMATRIX& d) { M4 r; memcpy(r.m, d.m, sizeof(r.m)); return r; }

static M4 Mul(const M4& a, const M4& b)
{
    M4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

static bool Inverse(const M4& in, M4& out)
{
    float a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            a[i][j] = j < 4 ? in.m[i][j] : (j - 4 == i ? 1.0f : 0.0f);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (fabsf(a[r][c]) > fabsf(a[p][c])) p = r;
        if (fabsf(a[p][c]) < 1e-12f) return false;
        if (p != c)
            for (int j = 0; j < 8; j++) { float t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        float inv = 1.0f / a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] *= inv;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            float f = a[r][c];
            for (int j = 0; j < 8; j++) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out.m[i][j] = a[i][j + 4];
    return true;
}

// ---------------------------------------------------------------- vertex shader classification

enum { VS_UNKNOWN = 0, VS_WVP = 1, VS_CAM = 2 };
struct VSInfo { IDirect3DVertexShader9* vs; int kind; int base; bool grass; };
static VSInfo g_vsInfo[256];
static int g_vsCount;

static void ClassifyShader(IDirect3DVertexShader9* vs, const DWORD* code)
{
    if (!vs || !code || g_vsCount >= 256) return;
    VSInfo info = { vs, VS_UNKNOWN, 0, false };
    bool skinned = false;
    int srcType = -1, base = -1;
    const DWORD* t = code + 1;                 // skip version token
    for (int guard = 0; guard < 4096 && *t != 0x0000FFFF; guard++) {
        DWORD tok = *t;
        DWORD op = tok & 0xFFFF;
        if (op == 0xFFFE) { t += 1 + ((tok >> 16) & 0x7FFF); continue; }   // comment block
        const DWORD* params = t + 1;
        int n = 0;
        while (params[n] & 0x80000000) n++;
        if (op == 0x1F && n >= 2) {                                         // dcl
            DWORD usage = params[0] & 0x1F, index = (params[0] >> 16) & 0xF;
            if (usage == D3DDECLUSAGE_BLENDWEIGHT) skinned = true;
            if (usage == D3DDECLUSAGE_POSITION && index == 1) info.grass = true;
        }
        if (op == 0x14 && n >= 3) {                                         // m4x4
            DWORD dst = params[0];
            int dType = ((dst >> 28) & 7) | ((dst >> 8) & 0x18);
            if (dType == D3DSPR_RASTOUT && (dst & 0x7FF) == 0) {            // oPos
                DWORD s0 = params[1], s1 = params[2];
                srcType = ((s0 >> 28) & 7) | ((s0 >> 8) & 0x18);
                int cType = ((s1 >> 28) & 7) | ((s1 >> 8) & 0x18);
                if (cType == D3DSPR_CONST) base = s1 & 0x7FF;
            }
        }
        t = params + n;
    }
    if (base >= 0 && base + 4 <= 96) {
        if (srcType == D3DSPR_INPUT) { info.kind = VS_WVP; info.base = base; }
        else if (skinned)            { info.kind = VS_CAM; info.base = base; }
    }
    g_vsInfo[g_vsCount++] = info;
    Log("Shader de vertices %d: tipo %s, matriz en c%d%s", g_vsCount,
        info.kind == VS_WVP ? "mundo*vista*proy" : info.kind == VS_CAM ? "animado (huesos)" : "desconocido",
        info.base, info.grass ? ", hierba" : "");
}

static const VSInfo* FindVS(IDirect3DVertexShader9* vs)
{
    for (int i = 0; i < g_vsCount; i++)
        if (g_vsInfo[i].vs == vs) return &g_vsInfo[i];
    return nullptr;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateVS)(IDirect3DDevice9*, const DWORD*, IDirect3DVertexShader9**);
static PFN_CreateVS oCreateVS;

static HRESULT STDMETHODCALLTYPE hkCreateVS(IDirect3DDevice9* d, const DWORD* code, IDirect3DVertexShader9** out)
{
    HRESULT hr = oCreateVS(d, code, out);
    if (!g_inFX && SUCCEEDED(hr) && out && *out) ClassifyShader(*out, code);
    return hr;
}

// Shadow copy of the vertex shader constants (the game sets them before each draw).
static float g_vsConst[96][4];

typedef HRESULT(STDMETHODCALLTYPE* PFN_SetVSConstF)(IDirect3DDevice9*, UINT, const float*, UINT);
static PFN_SetVSConstF oSetVSConstF;

static HRESULT STDMETHODCALLTYPE hkSetVSConstF(IDirect3DDevice9* d, UINT start, const float* data, UINT count)
{
    if (!g_inFX && data)
        for (UINT i = 0; i < count && start + i < 96; i++) memcpy(g_vsConst[start + i], data + i * 4, 16);
    return oSetVSConstF(d, start, data, count);
}

// ---------------------------------------------------------------- recording

struct ShadowDraw {
    bool indexed;
    D3DPRIMITIVETYPE pt;
    INT base; UINT minIdx, numV, start, count;
    IDirect3DVertexBuffer9* vb[2]; UINT off[2], stride[2];
    IDirect3DIndexBuffer9* ib;
    IDirect3DVertexDeclaration9* decl;
    DWORD fvf;
    IDirect3DVertexShader9* vs;
    const VSInfo* info;
    float c[96][4];
    D3DMATRIX world;
    IDirect3DBaseTexture9* tex;
    DWORD alphaTest, alphaRef, alphaFunc;
    // DrawPrimitiveUP / DrawIndexedPrimitiveUP: copies of the caller's data
    bool up;
    void* vdata; UINT vstride;
    void* idata; D3DFORMAT ifmt;
};
static const int MAX_SHADOW_DRAWS = 3000;
static ShadowDraw* g_sd;
static int g_sdCount, g_sdSkipped;
static int g_sdKind[3], g_sdNoZ, g_sdBlend;     // recorded per kind (FF, WVP, CAM) / filtered, for the log
static int g_sdUP;                              // of them, drawn from user memory (UP)
static D3DMATRIX g_curWorld, g_mainProj;

static void ReleaseShadowDraws()
{
    for (int i = 0; i < g_sdCount; i++) {
        ShadowDraw& r = g_sd[i];
        for (int k = 0; k < 2; k++) if (r.vb[k]) r.vb[k]->Release();
        if (r.ib) r.ib->Release();
        if (r.decl) r.decl->Release();
        if (r.vs) r.vs->Release();
        if (r.tex) r.tex->Release();
        free(r.vdata);
        free(r.idata);
    }
    g_sdCount = 0;
}

// Filters the draw and captures everything except the vertex/index source.
static ShadowDraw* BeginRecord(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt)
{
    if (g_inFX || !S.enabled || !S.shadows || g_depthDone || g_fxDone || !g_haveSun || !g_depthTex) return nullptr;
    if (pt == D3DPT_POINTLIST || pt == D3DPT_LINELIST || pt == D3DPT_LINESTRIP) return nullptr;
    DWORD zw = 0, ab = 0, ze = 0;
    d->GetRenderState(D3DRS_ZENABLE, &ze);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    if (!ze || !zw) { g_sdNoZ++; return nullptr; }    // sky, transparent effects (no depth write)
    if (ab) g_sdBlend++;                               // blended but writes depth (e.g. fading zombies): keep
    if (Is2D(d)) return nullptr;
    if (!g_sd) g_sd = (ShadowDraw*)calloc(MAX_SHADOW_DRAWS, sizeof(ShadowDraw));
    if (!g_sd || g_sdCount >= MAX_SHADOW_DRAWS) { g_sdSkipped++; return nullptr; }

    IDirect3DVertexShader9* vs = nullptr;
    d->GetVertexShader(&vs);
    const VSInfo* info = nullptr;
    if (vs) {
        info = FindVS(vs);
        if (!info || info->kind == VS_UNKNOWN || info->grass) { vs->Release(); g_sdSkipped++; return nullptr; }
    } else if (!g_curProjMain) {
        return nullptr;                                // fixed function with another camera
    }

    ShadowDraw& r = g_sd[g_sdCount++];
    memset(&r, 0, sizeof(r));
    r.pt = pt;
    d->GetVertexDeclaration(&r.decl);
    d->GetFVF(&r.fvf);
    r.vs = vs;
    r.info = info;
    if (vs) memcpy(r.c, g_vsConst, sizeof(r.c));
    r.world = g_curWorld;
    d->GetTexture(0, &r.tex);
    d->GetRenderState(D3DRS_ALPHATESTENABLE, &r.alphaTest);
    if (info && info->kind == VS_CAM) r.alphaTest = FALSE;   // animated models: solid, no cut-out
    g_sdKind[info ? info->kind : 0]++;
    d->GetRenderState(D3DRS_ALPHAREF, &r.alphaRef);
    d->GetRenderState(D3DRS_ALPHAFUNC, &r.alphaFunc);
    return &r;
}

static void RecordDraw(IDirect3DDevice9* d, bool indexed, D3DPRIMITIVETYPE pt, INT base, UINT minIdx,
                       UINT numV, UINT start, UINT count)
{
    ShadowDraw* r = BeginRecord(d, pt);
    if (!r) return;
    r->indexed = indexed; r->base = base; r->minIdx = minIdx; r->numV = numV; r->start = start; r->count = count;
    for (int k = 0; k < 2; k++) d->GetStreamSource(k, &r->vb[k], &r->off[k], &r->stride[k]);
    if (indexed) d->GetIndices(&r->ib);
}

static UINT IndexCount(D3DPRIMITIVETYPE pt, UINT prims)
{
    return pt == D3DPT_TRIANGLELIST ? prims * 3 : prims + 2;   // strips and fans
}

static void RecordDrawUP(IDirect3DDevice9* d, bool indexed, D3DPRIMITIVETYPE pt, UINT minIdx, UINT numV,
                         UINT prims, const void* idx, D3DFORMAT ifmt, const void* verts, UINT stride)
{
    if (!verts || !stride || (indexed && !idx)) return;
    ShadowDraw* r = BeginRecord(d, pt);
    if (!r) return;
    UINT nIdx = IndexCount(pt, prims);
    UINT nVerts = indexed ? minIdx + numV : nIdx;
    r->up = true; r->indexed = indexed; r->minIdx = minIdx; r->numV = numV; r->count = prims;
    r->vstride = stride;
    r->vdata = malloc((size_t)nVerts * stride);
    if (r->vdata) memcpy(r->vdata, verts, (size_t)nVerts * stride);
    if (indexed) {
        UINT isz = ifmt == D3DFMT_INDEX32 ? 4 : 2;
        r->ifmt = ifmt;
        r->idata = malloc((size_t)nIdx * isz);
        if (r->idata) memcpy(r->idata, idx, (size_t)nIdx * isz);
    }
    g_sdUP++;
}

// ---------------------------------------------------------------- sun camera

static M4 g_lightVP[2];         // world -> sun clip space, per cascade (0 = near, 1 = far)
static M4 g_invView;            // main camera view -> world
static float g_cascR[2];        // half size of each cascade (world units)

static bool ComputeLight(UINT size, int cascade, float R)
{
    M4 V = ToM4(g_mainView), P = ToM4(g_mainProj);
    if (!Inverse(V, g_invView)) return false;
    float cam[3] = { g_invView.m[3][0], g_invView.m[3][1], g_invView.m[3][2] };
    float fwd[3] = { g_invView.m[2][0], g_invView.m[2][1], g_invView.m[2][2] };
    g_cascR[cascade] = R;
    // centered on the camera (not ahead of it): turning the view does not move the cascades,
    // so shadows do not switch between the sharp and the coarse map while looking around
    float c[3] = { cam[0], cam[1], cam[2] };
    (void)fwd;

    // light basis: z = direction the light travels
    float z[3] = { g_sunDir[0], g_sunDir[1], g_sunDir[2] };
    float up[3] = { 0, 1, 0 };
    if (fabsf(z[1]) > 0.99f) { up[0] = 1; up[1] = 0; }
    float x[3] = { up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0] };
    float xl = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& v : x) v /= xl;
    float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };

    // snap the center to whole shadow-map texels so shadows do not shimmer when moving
    float texel = 2 * R / size;
    float cx = c[0] * x[0] + c[1] * x[1] + c[2] * x[2];
    float cy = c[0] * y[0] + c[1] * y[1] + c[2] * y[2];
    float cz = c[0] * z[0] + c[1] * z[1] + c[2] * z[2];
    cx = floorf(cx / texel) * texel;
    cy = floorf(cy / texel) * texel;
    const float D = 20000;                             // how far up-sun casters are captured
    M4 Vl = { { { x[0], y[0], z[0], 0 }, { x[1], y[1], z[1], 0 }, { x[2], y[2], z[2], 0 },
                { -cx, -cy, -(cz - D), 1 } } };
    M4 Pl = { { { 1 / R, 0, 0, 0 }, { 0, 1 / R, 0, 0 }, { 0, 0, 1 / (2 * D), 0 }, { 0, 0, 0, 1 } } };
    g_lightVP[cascade] = Mul(Vl, Pl);
    (void)P;
    return true;
}

// ---------------------------------------------------------------- shadow map rendering

static IDirect3DSurface9* g_smSurf[2];
static IDirect3DSurface9* g_smColor;    // required color target (writes disabled)
static UINT g_smSize;

static void ReleaseShadowMap()
{
    for (auto& sf : g_smSurf) if (sf) { sf->Release(); sf = nullptr; }
    if (g_smTex) { g_smTex->Release(); g_smTex = nullptr; }
    if (g_smTex2) { g_smTex2->Release(); g_smTex2 = nullptr; }
    if (g_smColor) { g_smColor->Release(); g_smColor = nullptr; }
    g_smSize = 0;
}

static bool CreateShadowMap(IDirect3DDevice9* dev)
{
    UINT size = (UINT)S.smSize;
    if (size < 512) size = 512;
    if (size > 8192) size = 8192;
    if (g_smTex && g_smSize == size) return true;
    ReleaseShadowMap();
    if (FAILED(dev->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL, FMT_INTZ, D3DPOOL_DEFAULT, &g_smTex, nullptr)) ||
        FAILED(g_smTex->GetSurfaceLevel(0, &g_smSurf[0])) ||
        FAILED(dev->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL, FMT_INTZ, D3DPOOL_DEFAULT, &g_smTex2, nullptr)) ||
        FAILED(g_smTex2->GetSurfaceLevel(0, &g_smSurf[1])) ||
        FAILED(dev->CreateRenderTarget(size, size, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_smColor, nullptr))) {
        ReleaseShadowMap();
        Log("No se pudo crear el mapa de sombras %ux%u", size, size);
        return false;
    }
    g_smSize = size;
    Log("Mapas de sombras (2 cascadas) %ux%u", size, size);
    return true;
}

static void WriteMatrixConsts(float (*c)[4], int base, const M4& m)
{
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) c[base + j][i] = m.m[i][j];
}

static M4 ReadMatrixConsts(const float (*c)[4], int base)
{
    M4 m;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) m.m[i][j] = c[base + j][i];
    return m;
}

static int g_smDrawn;

// Called inside RunFX (state already captured; the state block restores everything after).
static bool RenderShadowMap(IDirect3DDevice9* dev)
{
    g_lightOk = false;
    g_smDrawn = 0;
    if (!g_sdCount || !CreateShadowMap(dev) ||
        !ComputeLight(g_smSize, 0, S.smRange * 0.4f) || !ComputeLight(g_smSize, 1, S.smRange)) return false;

    M4 V = ToM4(g_mainView), P = ToM4(g_mainProj), VP = Mul(V, P), invVP;
    if (!Inverse(VP, invVP)) return false;
    dev->SetRenderTarget(0, g_smColor);
    dev->SetPixelShader(nullptr);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    float slope = 2.0f, bias = 0.00002f;
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD*)&slope);
    dev->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD*)&bias);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    for (int i = 1; i < 8; i++) dev->SetTexture(i, nullptr);

    D3DMATRIX lightP;
    memset(&lightP, 0, sizeof(lightP));
    lightP._11 = lightP._22 = lightP._33 = lightP._44 = 1;

    for (int cas = 0; cas < 2; cas++) {
    M4 camToLight = Mul(g_invView, g_lightVP[cas]);     // camera space -> sun clip
    M4 wvpToLight = Mul(invVP, g_lightVP[cas]);         // main clip -> sun clip
    D3DMATRIX lightV;
    memcpy(lightV.m, g_lightVP[cas].m, sizeof(lightV.m));  // fixed function: VIEW = sun VP, PROJ = identity
    dev->SetDepthStencilSurface(g_smSurf[cas]);
    D3DVIEWPORT9 vp = { 0, 0, g_smSize, g_smSize, 0, 1 };
    dev->SetViewport(&vp);
    dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);

    for (int i = 0; i < g_sdCount; i++) {
        ShadowDraw& r = g_sd[i];
        if (!r.up) {
            for (int k = 0; k < 2; k++) dev->SetStreamSource(k, r.vb[k], r.off[k], r.stride[k]);
            if (r.indexed) dev->SetIndices(r.ib);
        }
        if (r.decl) dev->SetVertexDeclaration(r.decl);
        else dev->SetFVF(r.fvf);
        dev->SetTexture(0, r.tex);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, r.alphaTest);
        dev->SetRenderState(D3DRS_ALPHAREF, r.alphaRef);
        dev->SetRenderState(D3DRS_ALPHAFUNC, r.alphaFunc);
        if (r.vs) {
            float c[96][4];
            memcpy(c, r.c, sizeof(c));
            M4 m = r.info->kind == VS_CAM ? camToLight
                                          : Mul(ReadMatrixConsts(r.c, r.info->base), wvpToLight);
            WriteMatrixConsts(c, r.info->base, m);
            dev->SetVertexShader(r.vs);
            dev->SetVertexShaderConstantF(0, &c[0][0], 96);
        } else {
            dev->SetVertexShader(nullptr);
            dev->SetTransform(D3DTS_WORLD, &r.world);
            dev->SetTransform(D3DTS_VIEW, &lightV);
            dev->SetTransform(D3DTS_PROJECTION, &lightP);
        }
        if (r.up) {
            if (!r.vdata || (r.indexed && !r.idata)) continue;
            if (r.indexed) dev->DrawIndexedPrimitiveUP(r.pt, r.minIdx, r.numV, r.count, r.idata, r.ifmt, r.vdata, r.vstride);
            else dev->DrawPrimitiveUP(r.pt, r.count, r.vdata, r.vstride);
        } else if (r.indexed) dev->DrawIndexedPrimitive(r.pt, r.base, r.minIdx, r.numV, r.start, r.count);
        else dev->DrawPrimitive(r.pt, r.start, r.count);
        if (cas == 0) g_smDrawn++;
    }
    }
    float zero = 0;
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *(DWORD*)&zero);
    dev->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD*)&zero);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetDepthStencilSurface(nullptr);
    for (int k = 0; k < 2; k++) dev->SetStreamSource(k, nullptr, 0, 0);
    dev->SetIndices(nullptr);
    dev->SetTexture(0, nullptr);
    g_lightOk = true;
    return true;
}

// c24..c32: per-pixel shadow lookup (columns of row-vector matrices)
static void SetShadowParams(IDirect3DDevice9* dev)
{
    float c[14][4];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            c[j][i] = g_invView.m[i][j];
            c[4 + j][i] = g_lightVP[0].m[i][j];
            c[9 + j][i] = g_lightVP[1].m[i][j];
        }
    c[8][0] = (float)g_smSize; c[8][1] = g_smSize ? 1.0f / g_smSize : 0; c[8][2] = S.smSoftness;
    c[8][3] = g_lightOk ? 1.0f : 0.0f;
    c[13][0] = g_cascR[0]; c[13][1] = g_cascR[1]; c[13][2] = 0; c[13][3] = 0;
    dev->SetPixelShaderConstantF(24, &c[0][0], 14);
}
