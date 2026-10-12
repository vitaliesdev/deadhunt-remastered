// Runtime compilation of DeadhuntFX.hlsl (F10 reloads). (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- shaders

typedef HRESULT(WINAPI* PFN_FXCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                        ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob**, ID3DBlob**);
static PFN_FXCompile g_D3DCompile;

enum { PS_FXAA, PS_BRIGHT, PS_DOWN, PS_BLURH, PS_BLURV, PS_FINAL,
       PS_SMAA_EDGES, PS_SMAA_WEIGHTS, PS_SMAA_BLEND, PS_AO, PS_AO_BLURH, PS_AO_BLURV, PS_DEPTH,
       PS_RAY_MASK, PS_RAY_BLUR, PS_STORE_Z, PS_FLASH_MASK, PS_FLASH_REDUCE, PS_ONE, PS_SHADOW, PS_COUNT };
static const char* kPsEntry[PS_COUNT] = { "PS_FXAA", "PS_Bright", "PS_Down", "PS_BlurH", "PS_BlurV",
                                          "PS_Final", "PS_SMAAEdges", "PS_SMAAWeights", "PS_SMAABlend",
                                          "PS_AO", "PS_AOBlurH", "PS_AOBlurV", "PS_Depth",
                                          "PS_RayMask", "PS_RayBlur", "PS_StoreZ", "PS_FlashMask",
                                          "PS_FlashReduce", "PS_One", "PS_Shadow" };
static char g_hlslPath[MAX_PATH];   // full path, so #include "DeadhuntFX/..." resolves

static IDirect3DVertexShader9*      g_vs;
static IDirect3DPixelShader9*       g_ps[PS_COUNT];
static IDirect3DVertexDeclaration9* g_decl;
static bool g_shadersOk;

static void ReleaseShaders()
{
    if (g_vs) { g_vs->Release(); g_vs = nullptr; }
    for (auto& p : g_ps)
        if (p) { p->Release(); p = nullptr; }
    if (g_decl) { g_decl->Release(); g_decl = nullptr; }
    g_shadersOk = false;
}

static ID3DBlob* Compile(const char* src, size_t len, const char* entry, const char* target)
{
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = g_D3DCompile(src, len, g_hlslPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        Log("Error compilando %s (%s): %s", entry, target,
            err ? (const char*)err->GetBufferPointer() : "?");
        if (code) code->Release();
        code = nullptr;
    }
    if (err) err->Release();
    return code;
}

static bool BuildShaders(IDirect3DDevice9* dev)
{
    ReleaseShaders();
    if (!g_D3DCompile) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) g_D3DCompile = (PFN_FXCompile)GetProcAddress(m, "D3DCompile");
        if (!g_D3DCompile) { Log("No se encontro d3dcompiler_47.dll"); return false; }
    }
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX.hlsl", g_dir);
    WideCharToMultiByte(CP_ACP, 0, path, -1, g_hlslPath, MAX_PATH, nullptr, nullptr);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) { Log("No se encontro DeadhuntFX.hlsl"); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* src = (char*)malloc(len + 1);
    fread(src, 1, len, f);
    src[len] = 0;
    fclose(f);

    bool ok = true;
    if (ID3DBlob* b = Compile(src, len, "VS_Quad", "vs_3_0")) {
        ok &= SUCCEEDED(dev->CreateVertexShader((const DWORD*)b->GetBufferPointer(), &g_vs));
        b->Release();
    } else ok = false;
    for (int i = 0; i < PS_COUNT && ok; i++) {
        if (ID3DBlob* b = Compile(src, len, kPsEntry[i], "ps_3_0")) {
            ok &= SUCCEEDED(dev->CreatePixelShader((const DWORD*)b->GetBufferPointer(), &g_ps[i]));
            b->Release();
        } else ok = false;
    }
    free(src);

    D3DVERTEXELEMENT9 el[] = {
        { 0, 0,  D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    ok = ok && SUCCEEDED(dev->CreateVertexDeclaration(el, &g_decl));
    if (!ok) { ReleaseShaders(); Log("Efectos desactivados: fallo al crear shaders"); }
    else Log("Shaders compilados correctamente");
    g_shadersOk = ok;
    return ok;
}

