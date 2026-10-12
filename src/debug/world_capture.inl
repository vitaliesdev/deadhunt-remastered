// DeadhuntFX - world diagnostics: on the F11 capture, every world draw is also written to
// DeadhuntFX_mundo.log in detail (shader, vertex layout, first vertices, texture stages, states),
// and each texture it uses is saved once as DeadhuntFX\captura\tex_<ptr>.dds.
// Used to find the terrain and grass draws. Included right after hud_capture.inl.

static FILE* g_worldLog;
static int g_worldLines;
static void* g_savedTex[512];
static int g_savedTexCount;

static void SaveTextureDDS(IDirect3DBaseTexture9* bt)
{
    if (!bt || bt->GetType() != D3DRTYPE_TEXTURE) return;
    for (int i = 0; i < g_savedTexCount; i++) if (g_savedTex[i] == bt) return;
    if (g_savedTexCount >= 512) return;
    g_savedTex[g_savedTexCount++] = bt;
    IDirect3DTexture9* t = (IDirect3DTexture9*)bt;
    D3DSURFACE_DESC desc;
    if (FAILED(t->GetLevelDesc(0, &desc)) || desc.Pool == D3DPOOL_DEFAULT) return;
    DWORD fourcc = 0;
    switch (desc.Format) {
    case D3DFMT_DXT1: fourcc = MAKEFOURCC('D', 'X', 'T', '1'); break;
    case D3DFMT_DXT3: fourcc = MAKEFOURCC('D', 'X', 'T', '3'); break;
    case D3DFMT_DXT5: fourcc = MAKEFOURCC('D', 'X', 'T', '5'); break;
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: break;
    default: return;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) return;
    UINT rows = fourcc ? (desc.Height + 3) / 4 : desc.Height;
    UINT rowBytes = fourcc ? ((desc.Width + 3) / 4) * (desc.Format == D3DFMT_DXT1 ? 8 : 16) : desc.Width * 4;
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    swprintf_s(dir, L"%sDeadhuntFX\\captura", g_dir);
    CreateDirectoryW(dir, nullptr);
    swprintf_s(path, L"%s\\tex_%p.dds", dir, (void*)bt);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") == 0 && f) {
        DWORD h[31] = {};
        h[0] = 124; h[1] = 0x1007 | (fourcc ? 0x80000 : 0x8); h[2] = desc.Height; h[3] = desc.Width;
        h[4] = fourcc ? rowBytes * rows : rowBytes;
        h[18] = 32;                                                     // pixel format block
        if (fourcc) { h[19] = 4; h[20] = fourcc; }
        else { h[19] = 0x41; h[21] = 32; h[22] = 0xFF0000; h[23] = 0xFF00; h[24] = 0xFF; h[25] = 0xFF000000; }
        h[26] = 0x1000;
        fwrite("DDS ", 4, 1, f);
        fwrite(h, 124, 1, f);
        for (UINT y = 0; y < rows; y++) fwrite((const char*)lr.pBits + (size_t)y * lr.Pitch, rowBytes, 1, f);
        fclose(f);
    }
    t->UnlockRect(0);
}

static const char* DeclUsageName(BYTE u)
{
    static const char* n[] = { "pos", "peso", "idxhueso", "normal", "tam", "uv", "tangente", "binormal",
                               "tess", "posT", "color", "niebla", "prof", "muestra" };
    return u < 14 ? n[u] : "?";
}

static void WorldLogDraw(IDirect3DDevice9* d, const char* api, D3DPRIMITIVETYPE pt, UINT prims,
                         const void* upVerts, UINT upStride, UINT firstVert)
{
    if (!g_hudCapture || g_inFX || g_fxDone || Is2D(d)) return;
    if (!g_worldLog) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%sDeadhuntFX_mundo.log", g_dir);
        if (_wfopen_s(&g_worldLog, path, L"w") != 0 || !g_worldLog) return;
        fprintf(g_worldLog, "==== Dibujos del mundo, pantalla %ux%u\n", g_w, g_h);
        g_worldLines = 0;
    }
    if (g_worldLines++ > 600) return;
    FILE* L = g_worldLog;

    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    d->GetVertexShader(&vs);
    d->GetPixelShader(&ps);
    const VSInfo* vi = vs ? FindVS(vs) : nullptr;
    int vsIdx = vi ? (int)(vi - g_vsInfo) + 1 : 0;
    DWORD fvf = 0;
    d->GetFVF(&fvf);
    fprintf(L, "#%d %s %s prims %u | VS %s%d%s | PS %s | FVF %X\n", g_worldLines, api,
            pt == D3DPT_TRIANGLELIST ? "lista" : pt == D3DPT_TRIANGLESTRIP ? "tira" : "otro", prims,
            vs ? "#" : "fijo", vsIdx, vi && vi->grass ? " (hierba)" : "", ps ? "si" : "no", fvf);

    // vertex layout
    D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1];
    UINT nel = MAXD3DDECLLENGTH + 1;
    IDirect3DVertexDeclaration9* decl = nullptr;
    if (!fvf && SUCCEEDED(d->GetVertexDeclaration(&decl)) && decl) {
        decl->GetDeclaration(el, &nel);
        fprintf(L, "   decl:");
        for (UINT i = 0; i + 1 < nel; i++)
            fprintf(L, " s%u+%u %s%u(t%u)", el[i].Stream, el[i].Offset, DeclUsageName(el[i].Usage), el[i].UsageIndex, el[i].Type);
        fputc('\n', L);
        decl->Release();
    }

    // first vertices of stream 0, raw floats
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT off = 0, stride = upStride;
    const unsigned char* data = (const unsigned char*)upVerts;
    void* locked = nullptr;
    if (!data) {
        d->GetStreamSource(0, &vb, &off, &stride);
        if (vb && stride && SUCCEEDED(vb->Lock(off + firstVert * stride, 3 * stride, &locked, D3DLOCK_READONLY)))
            data = (const unsigned char*)locked;
    } else data += (size_t)firstVert * stride;
    if (data && stride) {
        fprintf(L, "   stride %u\n", stride);
        for (int v = 0; v < 3; v++) {
            fprintf(L, "   v%d:", v);
            for (UINT k = 0; k < stride / 4 && k < 16; k++) fprintf(L, " %.3f", ((const float*)(data + (size_t)v * stride))[k]);
            fputc('\n', L);
        }
    }
    if (locked) vb->Unlock();
    if (vb) vb->Release();

    // texture stages
    for (DWORD s = 0; s < 4; s++) {
        IDirect3DBaseTexture9* t = nullptr;
        d->GetTexture(s, &t);
        DWORD cop = 0, a1 = 0, a2 = 0, aop = 0, tci = 0, ttf = 0;
        d->GetTextureStageState(s, D3DTSS_COLOROP, &cop);
        if (cop == D3DTOP_DISABLE && !t) break;
        d->GetTextureStageState(s, D3DTSS_COLORARG1, &a1);
        d->GetTextureStageState(s, D3DTSS_COLORARG2, &a2);
        d->GetTextureStageState(s, D3DTSS_ALPHAOP, &aop);
        d->GetTextureStageState(s, D3DTSS_TEXCOORDINDEX, &tci);
        d->GetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, &ttf);
        char tx[64] = "-";
        if (t && t->GetType() == D3DRTYPE_TEXTURE) {
            D3DSURFACE_DESC desc;
            ((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc);
            sprintf_s(tx, "tex_%p %ux%u fmt %d", (void*)t, desc.Width, desc.Height, (int)desc.Format);
            SaveTextureDDS(t);
        }
        fprintf(L, "   etapa%u: %s | colorop %u (%X,%X) alphaop %u uv %X ttf %u\n", s, tx, cop, a1, a2, aop, tci, ttf);
        if (ttf) {
            D3DMATRIX m;
            d->GetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &m);
            fprintf(L, "     matriz uv: %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f\n",
                    m._11, m._12, m._13, m._14, m._21, m._22, m._23, m._24, m._31, m._32, m._33, m._34,
                    m._41, m._42, m._43, m._44);
        }
        if (t) t->Release();
    }
    DWORD at, aref, ab, sb, db, zw, cull, fog, light, tf;
    d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
    d->GetRenderState(D3DRS_ALPHAREF, &aref);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    d->GetRenderState(D3DRS_SRCBLEND, &sb);
    d->GetRenderState(D3DRS_DESTBLEND, &db);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
    d->GetRenderState(D3DRS_CULLMODE, &cull);
    d->GetRenderState(D3DRS_FOGENABLE, &fog);
    d->GetRenderState(D3DRS_LIGHTING, &light);
    d->GetRenderState(D3DRS_TEXTUREFACTOR, &tf);
    fprintf(L, "   estados: alphatest %u ref %u | mezcla %u (%u,%u) | zwrite %u cull %u niebla %u luces %u tfactor %08X\n",
            at, aref, ab, sb, db, zw, cull, fog, light, tf);
    if (vs) {
        fprintf(L, "   c0-c4:");
        for (int c = 0; c < 5; c++) fprintf(L, " [%.3f %.3f %.3f %.3f]", g_vsConst[c][0], g_vsConst[c][1], g_vsConst[c][2], g_vsConst[c][3]);
        fputc('\n', L);
    } else {
        D3DMATRIX w;
        d->GetTransform(D3DTS_WORLD, &w);
        fprintf(L, "   mundo: pos %.1f %.1f %.1f  escala %.3f %.3f %.3f\n", w._41, w._42, w._43, w._11, w._22, w._33);
    }
    if (vs) vs->Release();
    if (ps) ps->Release();
}

static void WorldCaptureEnd()
{
    if (g_worldLog) { fprintf(g_worldLog, "---- fin (%d dibujos)\n", g_worldLines); fclose(g_worldLog); g_worldLog = nullptr; }
}
