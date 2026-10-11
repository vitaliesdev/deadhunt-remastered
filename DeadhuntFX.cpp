// DeadhuntFX - native post-processing for Deadhunt (Direct3D 9).
//
// Built as d3d9.dll in the game folder. Loads the real d3d9 implementation
// (dxvk_d3d9.dll if present, otherwise the system d3d9.dll), hooks
// IDirect3DDevice9::Present/Reset and runs a post-processing chain on the
// back buffer right before each frame is shown.
//
// Files next to the game exe:
//   DeadhuntFX.ini   settings (F10 reloads)
//   DeadhuntFX.hlsl  shaders  (F10 recompiles)
//   DeadhuntFX.log   errors / info

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <string>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <stdio.h>
#include <math.h>
#include <intrin.h>
#include <vector>
#include <algorithm>

// SMAA lookup textures (https://github.com/iryoku/smaa, MIT license)
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"

// ---------------------------------------------------------------- logging

static wchar_t g_dir[MAX_PATH];

static void Log(const char* fmt, ...)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sDeadhuntFX.log", g_dir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") != 0 || !f)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------- settings

struct Settings {
    bool  enabled;
    int   toggleKey, reloadKey;
    bool  smaa;
    bool  fxaa;
    float fxaaThreshold;
    bool  sharpen;
    float sharpenStrength;
    bool  bloom;
    float bloomThreshold, bloomIntensity, bloomRadius;
    bool  color;
    float exposure, gamma, contrast, saturation, vibrance, temperature;
    bool  vignette;
    float vignetteStrength, vignetteRadius;
    bool  grain;
    float grainStrength;
    bool  ao, aoDebug;
    int   debugView;
    float aoRadius, aoIntensity, aoBias, aoFadeStart, aoFadeEnd;
    bool  fog;
    float fogDensity, fogStart, fogMax, fogHeightFalloff, fogBaseHeight;
    bool  rays;
    float raysIntensity, raysDecay, raysLength, raysThreshold;
    bool  dof;
    float dofStart, dofEnd, dofStrength;
    bool  wpn;
    float wpnSpec, wpnShine, wpnRim, wpnAO;
    float handsSpec, handsShine, handsRim;
    UINT  handsW, handsH;   // size of the hands texture (0 = unknown: everything is "weapon")
    bool  hudFix, hudCover, hudMenus, hudTextFix;
    float hudScale, hudArea, hudTextScale;
    bool  shadows;
    float shLength, shThickness, shStrength, shFadeStart, shFadeEnd;
    float smRange, smSize, smSoftness;
    bool  flash;
    float flashStrength, flashRadius, flashWeapon, flashGain;
    float flashColor[3];
    bool  advEnabled;
    bool  hideHud;
    bool  newHud;
    bool  menuSkin;
};
static Settings S;

static float IniF(const wchar_t* sec, const wchar_t* key, float def)
{
    wchar_t path[MAX_PATH], buf[64], d[64];
    swprintf_s(path, L"%sDeadhuntFX.ini", g_dir);
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, path);
    return (float)_wtof(buf);
}

static void LoadSettings()
{
    S.enabled          = IniF(L"General", L"Enabled", 1) != 0;
    S.toggleKey        = (int)IniF(L"General", L"ToggleKey", VK_F9);
    S.reloadKey        = (int)IniF(L"General", L"ReloadKey", VK_F10);
    S.smaa             = IniF(L"SMAA", L"Enabled", 1) != 0;
    S.fxaa             = IniF(L"FXAA", L"Enabled", 0) != 0;
    S.fxaaThreshold    = IniF(L"FXAA", L"Threshold", 0.08f);
    S.sharpen          = IniF(L"Sharpen", L"Enabled", 1) != 0;
    S.sharpenStrength  = IniF(L"Sharpen", L"Strength", 0.6f);
    S.bloom            = IniF(L"Bloom", L"Enabled", 1) != 0;
    S.bloomThreshold   = IniF(L"Bloom", L"Threshold", 0.65f);
    S.bloomIntensity   = IniF(L"Bloom", L"Intensity", 0.35f);
    S.bloomRadius      = IniF(L"Bloom", L"Radius", 1.0f);
    S.color            = IniF(L"Color", L"Enabled", 1) != 0;
    S.exposure         = IniF(L"Color", L"Exposure", 1.0f);
    S.gamma            = IniF(L"Color", L"Gamma", 1.0f);
    S.contrast         = IniF(L"Color", L"Contrast", 1.08f);
    S.saturation       = IniF(L"Color", L"Saturation", 1.10f);
    S.vibrance         = IniF(L"Color", L"Vibrance", 0.20f);
    S.temperature      = IniF(L"Color", L"Temperature", 0.0f);
    S.vignette         = IniF(L"Vignette", L"Enabled", 1) != 0;
    S.vignetteStrength = IniF(L"Vignette", L"Strength", 0.35f);
    S.vignetteRadius   = IniF(L"Vignette", L"Radius", 0.55f);
    S.grain            = IniF(L"Grain", L"Enabled", 0) != 0;
    S.grainStrength    = IniF(L"Grain", L"Strength", 0.03f);
    S.ao               = IniF(L"AO", L"Enabled", 1) != 0;
    S.aoDebug          = IniF(L"Debug", L"ShowAO", 0) != 0;
    S.debugView        = (int)IniF(L"Debug", L"View", 0);
    S.aoRadius         = IniF(L"AO", L"Radius", 70.0f);
    S.aoIntensity      = IniF(L"AO", L"Intensity", 1.0f);
    S.aoBias           = IniF(L"AO", L"Bias", 0.15f);
    S.aoFadeStart      = IniF(L"AO", L"FadeStart", 2000.0f);
    S.aoFadeEnd        = IniF(L"AO", L"FadeEnd", 6000.0f);
    S.fog              = IniF(L"Fog", L"Enabled", 1) != 0;
    S.fogDensity       = IniF(L"Fog", L"Density", 0.00004f);
    S.fogStart         = IniF(L"Fog", L"Start", 2500.0f);
    S.fogMax           = IniF(L"Fog", L"MaxOpacity", 0.55f);
    S.fogHeightFalloff = IniF(L"Fog", L"HeightFalloff", 0.0004f);
    S.fogBaseHeight    = IniF(L"Fog", L"BaseHeight", 0.0f);
    S.rays             = IniF(L"GodRays", L"Enabled", 1) != 0;
    S.raysIntensity    = IniF(L"GodRays", L"Intensity", 0.6f);
    S.raysDecay        = IniF(L"GodRays", L"Decay", 0.97f);
    S.raysLength       = IniF(L"GodRays", L"Length", 0.85f);
    S.raysThreshold    = IniF(L"GodRays", L"Threshold", 0.45f);
    S.dof              = IniF(L"DOF", L"Enabled", 0) != 0;
    S.dofStart         = IniF(L"DOF", L"Start", 8000.0f);
    S.dofEnd           = IniF(L"DOF", L"End", 40000.0f);
    S.dofStrength      = IniF(L"DOF", L"Strength", 0.7f);
    S.wpn              = IniF(L"WeaponLight", L"Enabled", 1) != 0;
    S.wpnSpec          = IniF(L"WeaponLight", L"Specular", 0.6f);
    S.wpnShine         = IniF(L"WeaponLight", L"Shininess", 24.0f);
    S.wpnRim           = IniF(L"WeaponLight", L"Rim", 0.25f);
    S.wpnAO            = IniF(L"WeaponLight", L"SelfShadow", 0.6f);
    S.handsSpec        = IniF(L"WeaponLight", L"HandsSpecular", 0.08f);
    S.handsShine       = IniF(L"WeaponLight", L"HandsShininess", 6.0f);
    S.handsRim         = IniF(L"WeaponLight", L"HandsRim", 0.12f);
    S.handsW           = (UINT)IniF(L"WeaponLight", L"HandsTextureWidth", 0);
    S.handsH           = (UINT)IniF(L"WeaponLight", L"HandsTextureHeight", 0);
    S.hudFix           = IniF(L"HUD", L"Enabled", 1) != 0;
    S.hudScale         = IniF(L"HUD", L"Scale", 1.0f);
    S.hudArea          = IniF(L"HUD", L"Area", 0.0f);
    S.hudCover         = IniF(L"HUD", L"CoverBackgrounds", 1) != 0;
    S.hudMenus         = IniF(L"HUD", L"Menus", 0) != 0;
    S.hudTextScale     = IniF(L"HUD", L"TextScale", 1.0f);
    S.hudTextFix       = IniF(L"HUD", L"TextFix", 1) != 0;
    // patch_textsize.py makes the engine size its fonts by the screen height itself
    if (!IsBadReadPtr((void*)0x5432c4, 6) && *(unsigned char*)0x5432c6 == 0xd4) S.hudTextFix = false;
    S.shadows          = IniF(L"Shadows", L"Enabled", 1) != 0;
    S.shLength         = IniF(L"Shadows", L"Length", 1200.0f);
    S.shThickness      = IniF(L"Shadows", L"Thickness", 120.0f);
    S.shStrength       = IniF(L"Shadows", L"Strength", 0.5f);
    S.shFadeStart      = IniF(L"Shadows", L"FadeStart", 3000.0f);
    S.shFadeEnd        = IniF(L"Shadows", L"FadeEnd", 9000.0f);
    S.smRange          = IniF(L"Shadows", L"Range", 3000.0f);
    S.smSize           = IniF(L"Shadows", L"MapSize", 2048.0f);
    S.smSoftness       = IniF(L"Shadows", L"Softness", 1.5f);
    S.flash            = IniF(L"MuzzleFlash", L"Enabled", 1) != 0;
    S.flashStrength    = IniF(L"MuzzleFlash", L"Strength", 1.5f);
    S.flashRadius      = IniF(L"MuzzleFlash", L"Radius", 700.0f);
    S.flashWeapon      = IniF(L"MuzzleFlash", L"WeaponStrength", 0.8f);
    S.flashGain        = IniF(L"MuzzleFlash", L"Sensitivity", 40.0f);
    S.flashColor[0]    = IniF(L"MuzzleFlash", L"ColorR", 1.0f);
    S.flashColor[1]    = IniF(L"MuzzleFlash", L"ColorG", 0.72f);
    S.flashColor[2]    = IniF(L"MuzzleFlash", L"ColorB", 0.42f);
    S.advEnabled       = IniF(L"Advanced", L"Enabled", 1) != 0;
    S.hideHud          = IniF(L"Debug", L"HideHUD", 0) != 0;
    S.newHud           = IniF(L"HUD", L"New", 1) != 0;
    S.menuSkin         = IniF(L"HUD", L"MenuSkin", 1) != 0;
}

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

// ---------------------------------------------------------------- render targets

struct RT {
    IDirect3DTexture9* tex = nullptr;
    IDirect3DSurface9* surf = nullptr;
    UINT w = 0, h = 0;
};
static RT g_scene, g_aa, g_q1, g_q2, g_e1, g_e2, g_edges, g_weights, g_ao1, g_ao2;
static RT g_ray1, g_ray2, g_dof1, g_dof2;   // quarter resolution
static RT g_worldZ;                         // world linear depth, saved before the game clears it
static RT g_fl32, g_fl4, g_fl1;             // muzzle flash detection: 32x16 -> 4x2 -> 1x1
static RT g_handMask;                       // 1 where the hands were drawn (from stencil tags)
static RT g_sh1, g_sh2;                     // screen-space sun shadows (half resolution)
static bool RenderShadowMap(IDirect3DDevice9* dev);
static void SetShadowParams(IDirect3DDevice9* dev);
static void ReleaseShadowMap();
static void ReleaseShadowDraws();
static IDirect3DTexture9* g_smTex;      // shadow map (INTZ), near cascade
static IDirect3DTexture9* g_smTex2;     // far cascade
static bool g_lightOk;

static bool g_worldZValid;                  // g_worldZ was written this frame

// Readable scene depth: the game's auto depth buffer is replaced by an INTZ texture.
static const D3DFORMAT FMT_INTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
static IDirect3DTexture9* g_depthTex;
static IDirect3DSurface9* g_depthSurf;
static bool g_depthWanted;      // we disabled the auto depth buffer and own the depth surface
static D3DFORMAT g_depthFallbackFmt = D3DFMT_D24S8;

// Camera captured from SetTransform (main 3D projection of the current frame).
static float g_zn = 50, g_zf = 100000, g_invP11 = 1, g_invP22 = 1;
static float g_up[4];           // world-up row of the inverse view matrix
static D3DMATRIX g_mainView;    // view matrix of the main camera
static float g_wzn = 10, g_wzf = 500, g_winvP11 = 0, g_winvP22 = 0.3f;   // weapon camera
static float g_sunView[3];      // direction towards the sun in view space
static bool g_sunInView;
static bool g_haveSun;          // a directional light (the sun) was captured
static float g_sunDir[3], g_sunCol[3] = { 1, 0.95f, 0.85f };
static D3DMATRIX g_lastView;
static D3DMATRIX g_curProjM;     // projection currently set (any)
static bool g_curProjMain, g_have3D, g_fxDone, g_depthDone, g_inScene;   // g_have3D: a main camera was captured (persistent)
static int g_drawsSince3D;                        // world draws this frame (non screen-space)
static int g_hudByFvf, g_hudByProj, g_depthStages;
static IDirect3DTexture9 *g_areaTex, *g_searchTex;   // MANAGED: survive Reset
static IDirect3DStateBlock9* g_sb;
static UINT g_w, g_h;
static bool g_resOk;

static void FreeRT(RT& r)
{
    if (r.surf) r.surf->Release();
    if (r.tex) r.tex->Release();
    r = RT();
}

static void ReleaseResources()
{
    FreeRT(g_scene); FreeRT(g_aa); FreeRT(g_q1); FreeRT(g_q2); FreeRT(g_e1); FreeRT(g_e2);
    FreeRT(g_edges); FreeRT(g_weights); FreeRT(g_ao1); FreeRT(g_ao2);
    FreeRT(g_ray1); FreeRT(g_ray2); FreeRT(g_dof1); FreeRT(g_dof2);
    FreeRT(g_worldZ); FreeRT(g_fl32); FreeRT(g_fl4); FreeRT(g_fl1); FreeRT(g_handMask);
    FreeRT(g_sh1); FreeRT(g_sh2);
    ReleaseShadowMap();
    if (g_sb) { g_sb->Release(); g_sb = nullptr; }
    g_resOk = false;
    g_w = g_h = 0;
}

static bool MakeRT(IDirect3DDevice9* dev, RT& r, UINT w, UINT h, D3DFORMAT fmt, D3DFORMAT fallback)
{
    r.w = w ? w : 1;
    r.h = h ? h : 1;
    if (FAILED(dev->CreateTexture(r.w, r.h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &r.tex, nullptr)) &&
        FAILED(dev->CreateTexture(r.w, r.h, 1, D3DUSAGE_RENDERTARGET, fallback, D3DPOOL_DEFAULT, &r.tex, nullptr)))
        return false;
    return SUCCEEDED(r.tex->GetSurfaceLevel(0, &r.surf));
}

static IDirect3DTexture9* MakeLookup(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt,
                                     const unsigned char* bytes, UINT pitch)
{
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED, &t, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(t->LockRect(0, &lr, nullptr, 0))) {
        for (UINT y = 0; y < h; y++)
            memcpy((unsigned char*)lr.pBits + y * lr.Pitch, bytes + y * pitch, pitch);
        t->UnlockRect(0);
    }
    return t;
}

static bool CreateResources(IDirect3DDevice9* dev, const D3DSURFACE_DESC& bb)
{
    // SMAA AreaTex is R8G8 -> A8L8 (red in L, green in A: read as .ra in SMAA_HLSL_3)
    if (!g_areaTex)
        g_areaTex = MakeLookup(dev, AREATEX_WIDTH, AREATEX_HEIGHT, D3DFMT_A8L8, areaTexBytes, AREATEX_PITCH);
    if (!g_searchTex)
        g_searchTex = MakeLookup(dev, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, D3DFMT_L8, searchTexBytes, SEARCHTEX_PITCH);
    ReleaseResources();
    g_w = bb.Width;
    g_h = bb.Height;
    // patch_label.py: Options shows this when the resolution is not in the monitor's mode list
    {
        static const unsigned char cave[6] = { 0x8b, 0x45, 0xf0, 0x8b, 0x48, 0x30 };
        char* label = (char*)0x676260;
        if (!IsBadReadPtr((void*)0x676240, 0x30) && !memcmp((void*)0x676240, cave, 6)) {
            DWORD old;
            VirtualProtect(label, 16, PAGE_EXECUTE_READWRITE, &old);
            sprintf_s(label, 16, "%ux%u", g_w, g_h);
            VirtualProtect(label, 16, old, &old);
        }
    }
    const D3DFORMAT hdr = D3DFMT_A16B16G16R16F;
    bool ok = MakeRT(dev, g_scene, g_w, g_h, bb.Format, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_aa, g_w, g_h, D3DFMT_A8R8G8B8, bb.Format)
           && MakeRT(dev, g_q1, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_q2, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_e1, g_w / 16, g_h / 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_e2, g_w / 16, g_h / 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_edges, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_weights, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ao1, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ao2, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ray1, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_ray2, g_w / 4, g_h / 4, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_dof1, g_w / 4, g_h / 4, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_dof2, g_w / 4, g_h / 4, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_worldZ, g_w, g_h, D3DFMT_R32F, hdr)
           && MakeRT(dev, g_fl32, 32, 16, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_fl4, 4, 2, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_fl1, 1, 1, hdr, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_handMask, g_w, g_h, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_sh1, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && MakeRT(dev, g_sh2, g_w / 2, g_h / 2, D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8)
           && g_areaTex && g_searchTex
           && SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &g_sb));
    if (!ok) { ReleaseResources(); Log("No se pudieron crear las texturas de efectos (%ux%u)", g_w, g_h); }
    g_resOk = ok;
    return ok;
}

// ---------------------------------------------------------------- drawing

struct Vtx { float x, y, z, w, u, v; };

static void Pass(IDirect3DDevice9* dev, int ps, IDirect3DSurface9* target, UINT tw, UINT th,
                 IDirect3DTexture9* t0, UINT sw, UINT sh,
                 IDirect3DTexture9* t1 = nullptr, IDirect3DTexture9* t2 = nullptr)
{
    dev->SetRenderTarget(0, target);
    float off[4] = { -1.0f / tw, 1.0f / th, 0, 0 };   // D3D9 half-pixel offset
    dev->SetVertexShaderConstantF(0, off, 1);
    float texel[4] = { 1.0f / sw, 1.0f / sh, (float)sw, (float)sh };
    dev->SetPixelShaderConstantF(0, texel, 1);
    dev->SetPixelShader(g_ps[ps]);
    dev->SetTexture(0, t0);
    dev->SetTexture(1, t1);
    dev->SetTexture(2, t2);
    static const Vtx q[4] = {
        { -1,  1, 0, 1, 0, 0 }, { 1,  1, 0, 1, 1, 0 },
        { -1, -1, 0, 1, 0, 1 }, { 1, -1, 0, 1, 1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

static void SetupState(IDirect3DDevice9* dev)
{
    dev->SetVertexDeclaration(g_decl);
    dev->SetVertexShader(g_vs);
    dev->SetDepthStencilSurface(nullptr);
    for (int i = 1; i < 4; i++) dev->SetRenderTarget(i, nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    for (int s = 0; s < 16; s++) {
        DWORD filt = (s == 4 || s == 7 || s >= 14) ? D3DTEXF_POINT : D3DTEXF_LINEAR;   // SearchTex, depths
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, filt);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, filt);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
        dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
    }
}

static void SetParams(IDirect3DDevice9* dev)
{
    static LARGE_INTEGER f0, freq;
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&f0); }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float t = (float)((now.QuadPart - f0.QuadPart) / (double)freq.QuadPart);
    t = fmodf(t, 1000.0f);

    // c1..c5: effect parameters (disabled effects get neutral values)
    float c[5][4] = {
        { S.sharpen ? S.sharpenStrength : 0, S.bloom ? S.bloomIntensity : 0, S.bloomThreshold, t },
        { S.color ? S.exposure : 1, S.color ? S.gamma : 1, S.color ? S.contrast : 1, S.color ? S.saturation : 1 },
        { S.color ? S.vibrance : 0, S.color ? S.temperature : 0,
          S.vignette ? S.vignetteStrength : 0, S.vignetteRadius },
        { S.grain ? S.grainStrength : 0, (float)g_w / (float)(g_h ? g_h : 1), S.bloomRadius, S.fxaaThreshold },
    };
    dev->SetPixelShaderConstantF(1, &c[0][0], 4);
}

// c5..c9: depth/camera based effects. depthOn = depth texture valid for this frame.
static void SetDepthParams(IDirect3DDevice9* dev, bool depthOn)
{
    DWORD fc = 0x808080;
    dev->GetRenderState(D3DRS_FOGCOLOR, &fc);
    float c[7][4] = {
        { g_zn, g_zf, g_invP11, g_invP22 },
        { S.aoRadius, S.aoIntensity, S.aoBias, (depthOn && S.ao) ? (S.aoDebug ? 2.0f : 1.0f) : 0.0f },
        { S.fogDensity, S.fogHeightFalloff, S.fogStart, (depthOn && S.fog) ? S.fogMax : 0.0f },
        { ((fc >> 16) & 255) / 255.0f, ((fc >> 8) & 255) / 255.0f, (fc & 255) / 255.0f, S.fogBaseHeight },
        { g_up[0], g_up[1], g_up[2], g_up[3] },
        { S.aoFadeStart, S.aoFadeEnd, (float)S.debugView, 0 },
        { 1.0f / g_w, 1.0f / g_h, (float)g_w, (float)g_h },   // depth buffer = back buffer size
    };
    dev->SetPixelShaderConstantF(5, &c[0][0], 7);

    // Sun on screen: direction towards the sun, rotated into view space and projected.
    float sunU = 0.5f, sunV = 0.5f, vis = 0;
    if (g_haveSun) {
        const D3DMATRIX& v = g_mainView;
        float d[3] = { -g_sunDir[0], -g_sunDir[1], -g_sunDir[2] };
        float vx = d[0] * v._11 + d[1] * v._21 + d[2] * v._31;
        float vy = d[0] * v._12 + d[1] * v._22 + d[2] * v._32;
        float vz = d[0] * v._13 + d[1] * v._23 + d[2] * v._33;
        g_sunView[0] = vx; g_sunView[1] = vy; g_sunView[2] = vz;
        g_sunInView = true;
        if (vz > 0.05f) {
            float nx = vx / (vz * g_invP11), ny = vy / (vz * g_invP22);
            sunU = nx * 0.5f + 0.5f;
            sunV = 0.5f - ny * 0.5f;
            float off = fmaxf(fmaxf(-sunU, sunU - 1), fmaxf(-sunV, sunV - 1));   // > 0 outside the screen
            vis = 1.0f - fminf(fmaxf(off / 0.6f, 0.0f), 1.0f);
            vis *= fminf((vz - 0.05f) / 0.25f, 1.0f);
        }
    }
    float r[3][4] = {
        { sunU, sunV, S.rays ? S.raysIntensity * vis : 0.0f, S.raysDecay },
        { g_sunCol[0], g_sunCol[1], g_sunCol[2], S.raysLength },
        { S.dofStart, S.dofEnd, S.dof ? S.dofStrength : 0.0f, S.raysThreshold },
    };
    dev->SetPixelShaderConstantF(12, &r[0][0], 3);

    // c22..c23: screen-space sun shadows (direction to the sun in view space)
    float sl = sqrtf(g_sunView[0] * g_sunView[0] + g_sunView[1] * g_sunView[1] + g_sunView[2] * g_sunView[2]);
    bool shOn = depthOn && S.shadows && g_haveSun && sl > 1e-4f;
    float sh[2][4] = {
        { shOn ? g_sunView[0] / sl : 0, shOn ? g_sunView[1] / sl : 0, shOn ? g_sunView[2] / sl : 0,
          shOn ? S.shStrength : 0.0f },
        { S.smRange, S.shThickness, S.shFadeStart, S.shFadeEnd },
    };
    dev->SetPixelShaderConstantF(22, &sh[0][0], 2);
}

// c15..c20: weapon lighting and muzzle flash (post stage).
static void SetWeaponParams(IDirect3DDevice9* dev, bool on, float grid[2])
{
    float aspect = (float)g_w / (float)(g_h ? g_h : 1);
    float wP11 = g_winvP11 > 0 ? g_winvP11 : 0.3f * aspect;   // the Hor+ patch makes width = 0.6*aspect*zn
    float c[7][4] = {
        { grid[0], grid[1], 0, 0 },
        { g_wzn, g_wzf, wP11, g_winvP22 },
        { g_sunView[0], g_sunView[1], g_sunView[2], (on && S.wpn && g_sunInView) ? 1.0f : 0.0f },
        { S.wpnSpec, S.wpnShine, S.wpnRim, S.wpnAO },
        { (on && S.flash && g_worldZValid) ? S.flashStrength : 0.0f, S.flashRadius, S.flashWeapon, S.flashGain },
        { S.flashColor[0], S.flashColor[1], S.flashColor[2], on ? 1.0f : 0.0f },
        { S.handsSpec, S.handsShine, S.handsRim, (on && S.handsW) ? 1.0f : 0.0f },
    };
    dev->SetPixelShaderConstantF(15, &c[0][0], 7);
}

// Effect stages:
//  STAGE_DEPTH: ambient occlusion + fog. Must run while the world is still in the depth
//               buffer: the game clears depth before drawing the weapon.
//  STAGE_POST:  SMAA, bloom, sharpen, color, vignette, grain (right before the HUD).
enum { STAGE_DEPTH = 1, STAGE_POST = 2, STAGE_ALL = 3 };

// Runs effects on the current render target.
static void RunFX(IDirect3DDevice9* dev, int stage)
{
    bool midScene = g_inScene;
    if (dev->TestCooperativeLevel() != D3D_OK)
        return;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &bb)) || !bb)
        return;
    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);
    if (!g_shadersOk && !BuildShaders(dev)) { bb->Release(); S.enabled = false; return; }
    if ((!g_resOk || desc.Width != g_w || desc.Height != g_h) && !CreateResources(dev, desc)) {
        bb->Release(); S.enabled = false; return;
    }

    IDirect3DSurface9 *oldRT = nullptr, *oldDS = nullptr;
    dev->GetRenderTarget(0, &oldRT);
    dev->GetDepthStencilSurface(&oldDS);
    g_sb->Capture();

    dev->StretchRect(bb, nullptr, g_scene.surf, nullptr, D3DTEXF_NONE);   // resolves MSAA
    if (!midScene) dev->BeginScene();
    if ((stage & STAGE_DEPTH) && S.shadows && g_haveSun && g_depthTex && g_have3D && g_drawsSince3D > 0)
        RenderShadowMap(dev);
    SetupState(dev);
    SetParams(dev);
    bool depthOn = (stage & STAGE_DEPTH) && g_depthTex && g_have3D && g_drawsSince3D > 0 &&
                   (S.ao || S.fog || S.rays || S.dof || S.shadows);
    if (depthOn) {
        SetDepthParams(dev, true);
        dev->SetTexture(7, g_depthTex);
        dev->SetTexture(6, nullptr);
        if (S.ao) {
            Pass(dev, PS_AO, g_ao1.surf, g_ao1.w, g_ao1.h, nullptr, g_w, g_h);
            Pass(dev, PS_AO_BLURH, g_ao2.surf, g_ao2.w, g_ao2.h, g_ao1.tex, g_ao1.w, g_ao1.h);
            Pass(dev, PS_AO_BLURV, g_ao1.surf, g_ao1.w, g_ao1.h, g_ao2.tex, g_ao2.w, g_ao2.h);
            dev->SetTexture(6, g_ao1.tex);
        }
        dev->SetTexture(8, nullptr);
        dev->SetTexture(9, nullptr);
        dev->SetTexture(13, nullptr);
        if (S.shadows && g_haveSun && g_lightOk) {
            SetShadowParams(dev);
            dev->SetTexture(14, g_smTex);
            dev->SetTexture(15, g_smTex2);
            Pass(dev, PS_SHADOW, g_sh1.surf, g_sh1.w, g_sh1.h, nullptr, g_w, g_h);
            dev->SetTexture(14, nullptr);
            dev->SetTexture(15, nullptr);
            Pass(dev, PS_AO_BLURH, g_sh2.surf, g_sh2.w, g_sh2.h, g_sh1.tex, g_sh1.w, g_sh1.h);
            Pass(dev, PS_AO_BLURV, g_sh1.surf, g_sh1.w, g_sh1.h, g_sh2.tex, g_sh2.w, g_sh2.h);
            dev->SetTexture(13, g_sh1.tex);
        }
        if (S.rays && g_haveSun) {
            Pass(dev, PS_RAY_MASK, g_ray1.surf, g_ray1.w, g_ray1.h, g_scene.tex, g_w, g_h);
            Pass(dev, PS_RAY_BLUR, g_ray2.surf, g_ray2.w, g_ray2.h, g_ray1.tex, g_ray1.w, g_ray1.h);
            dev->SetTexture(8, g_ray2.tex);
        }
        if (S.dof) {
            Pass(dev, PS_DOWN, g_dof1.surf, g_dof1.w, g_dof1.h, g_scene.tex, g_w, g_h);
            Pass(dev, PS_BLURH, g_dof2.surf, g_dof2.w, g_dof2.h, g_dof1.tex, g_dof1.w, g_dof1.h);
            Pass(dev, PS_BLURV, g_dof1.surf, g_dof1.w, g_dof1.h, g_dof2.tex, g_dof2.w, g_dof2.h);
            dev->SetTexture(9, g_dof1.tex);
        }
        Pass(dev, PS_DEPTH, bb, g_w, g_h, g_scene.tex, g_w, g_h);
        if (S.flash) {
            Pass(dev, PS_STORE_Z, g_worldZ.surf, g_w, g_h, nullptr, g_w, g_h);
            g_worldZValid = true;
        }
        for (int i = 6; i < 10; i++) dev->SetTexture(i, nullptr);
        dev->SetTexture(13, nullptr);
        if (stage & STAGE_POST)   // post effects need the image with AO/fog applied
            dev->StretchRect(bb, nullptr, g_scene.surf, nullptr, D3DTEXF_NONE);
    }

    RT* src = &g_scene;
    if (!(stage & STAGE_POST)) {
        // depth stage only
    } else if (S.smaa) {
        dev->SetTexture(3, g_areaTex);
        dev->SetTexture(4, g_searchTex);
        dev->SetRenderTarget(0, g_edges.surf);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        Pass(dev, PS_SMAA_EDGES, g_edges.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        dev->SetRenderTarget(0, g_weights.surf);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        Pass(dev, PS_SMAA_WEIGHTS, g_weights.surf, g_w, g_h, g_edges.tex, g_w, g_h);
        dev->SetTexture(5, g_weights.tex);
        Pass(dev, PS_SMAA_BLEND, g_aa.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        dev->SetTexture(5, nullptr);
        src = &g_aa;
    } else if (S.fxaa) {
        Pass(dev, PS_FXAA, g_aa.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        src = &g_aa;
    }
    if ((stage & STAGE_POST) && S.bloom) {
        Pass(dev, PS_BRIGHT, g_q1.surf, g_q1.w, g_q1.h, src->tex, src->w, src->h);
        Pass(dev, PS_BLURH, g_q2.surf, g_q2.w, g_q2.h, g_q1.tex, g_q1.w, g_q1.h);
        Pass(dev, PS_BLURV, g_q1.surf, g_q1.w, g_q1.h, g_q2.tex, g_q2.w, g_q2.h);
        Pass(dev, PS_DOWN, g_e1.surf, g_e1.w, g_e1.h, g_q1.tex, g_q1.w, g_q1.h);
        Pass(dev, PS_BLURH, g_e2.surf, g_e2.w, g_e2.h, g_e1.tex, g_e1.w, g_e1.h);
        Pass(dev, PS_BLURV, g_e1.surf, g_e1.w, g_e1.h, g_e2.tex, g_e2.w, g_e2.h);
    }
    if (stage & STAGE_POST) {
        // After the depth stage the depth buffer holds only the weapon.
        bool wpnOn = g_depthDone && g_depthTex && (S.wpn || S.flash);
        float grid[2] = { 8, 8 };
        SetDepthParams(dev, false);
        SetWeaponParams(dev, wpnOn, grid);
        if (wpnOn && S.flash && g_worldZValid) {
            Pass(dev, PS_FLASH_MASK, g_fl32.surf, 32, 16, g_scene.tex, g_w, g_h);
            Pass(dev, PS_FLASH_REDUCE, g_fl4.surf, 4, 2, g_fl32.tex, 32, 16);
            grid[0] = 4; grid[1] = 2;
            SetWeaponParams(dev, wpnOn, grid);
            Pass(dev, PS_FLASH_REDUCE, g_fl1.surf, 1, 1, g_fl4.tex, 4, 2);
            dev->SetTexture(10, g_fl1.tex);
            dev->SetTexture(11, g_worldZ.tex);
        }
        if (wpnOn && S.wpn && S.handsW) {
            // Stencil == 2 marks the hands (tagged while the game drew them). The depth
            // surface is bound only for the stencil test; this pass does not sample it.
            dev->SetRenderTarget(0, g_handMask.surf);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            dev->SetDepthStencilSurface(g_depthSurf);
            dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
            dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
            dev->SetRenderState(D3DRS_STENCILREF, 2);
            dev->SetRenderState(D3DRS_STENCILMASK, 0xFF);
            dev->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
            dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
            Pass(dev, PS_ONE, g_handMask.surf, g_w, g_h, nullptr, g_w, g_h);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetDepthStencilSurface(nullptr);
            dev->SetTexture(12, g_handMask.tex);
        }
        dev->SetTexture(7, wpnOn ? g_depthTex : nullptr);
        Pass(dev, PS_FINAL, bb, g_w, g_h, src->tex, src->w, src->h, g_q1.tex, g_e1.tex);
        dev->SetTexture(10, nullptr);
        dev->SetTexture(11, nullptr);
        dev->SetTexture(12, nullptr);
    }
    for (int i = 3; i < 15; i++) dev->SetTexture(i, nullptr);
    if (!midScene) dev->EndScene();

    g_sb->Apply();
    dev->SetRenderTarget(0, oldRT);
    dev->SetDepthStencilSurface(oldDS);
    if (oldRT) oldRT->Release();
    if (oldDS) oldDS->Release();
    bb->Release();
}

// ---------------------------------------------------------------- hooks

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                     D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static PFN_CreateDevice oCreateDevice;
static PFN_Present oPresent;
static PFN_Reset oReset;
static HWND g_hwnd;

static void* HookVtable(void* obj, int index, void* fn)
{
    void** vt = *(void***)obj;
    if (vt[index] == fn) return nullptr;
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* orig = vt[index];
    vt[index] = fn;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return orig;
}

static bool KeyPressed(int vk, bool& was)
{
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool fg = false;
    if (HWND w = GetForegroundWindow()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        fg = pid == GetCurrentProcessId();
    }
    bool hit = fg && down && !was;
    was = down;
    return hit;
}

// ---------------------------------------------------------------- profiler
// [Debug] Profile=1: logs where the time goes in slow frames.

static bool g_profile, g_inFX;
static double g_qpcMs;

static double NowMs()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * g_qpcMs;
}

struct FrameStats {
    int draws, drawsUP, prims;
    int vbLocks, vbLocksSync, ibLocks, ibLocksSync, texLocks;
    int creates;
    double drawMs, lockMs, texLockMs, createMs;
};
static FrameStats F;

struct VtOrig { void** vt; void* orig; };
static VtOrig g_lockOrig[16];

static void* OrigFor(void* obj)
{
    void** vt = *(void***)obj;
    for (auto& e : g_lockOrig)
        if (e.vt == vt) return e.orig;
    return nullptr;
}

static void HookLock(void* obj, int index, void* fn)
{
    void** vt = *(void***)obj;
    for (auto& e : g_lockOrig)
        if (e.vt == vt) return;
    for (auto& e : g_lockOrig)
        if (!e.vt) {
            e.vt = vt;
            e.orig = HookVtable(obj, index, fn);
            return;
        }
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_BufLock)(void*, UINT, UINT, void**, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* PFN_TexLock)(void*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);

// [Buffers] fix for CPU<->GPU sync stalls on buffer locks (see hkCVB/hkCIB/LockCommon)
static bool g_bufFix;

// Waiting locks grouped by buffer kind, for the profiler log.
struct LockCat { DWORD pool, usage; bool whole; int count; double ms; };
static LockCat g_cats[8];

static void CountCat(DWORD pool, DWORD usage, bool whole, double ms)
{
    for (auto& c : g_cats) {
        if (c.count && c.pool == pool && c.usage == usage && c.whole == whole) { c.count++; c.ms += ms; return; }
    }
    for (auto& c : g_cats) {
        if (!c.count) { c = { pool, usage, whole, 1, ms }; return; }
    }
}

static HRESULT LockCommon(void* self, UINT o, UINT s, void** p, DWORD fl, bool vb)
{
    const DWORD noWait = D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE | D3DLOCK_READONLY;
    bool sync = !(fl & noWait);
    DWORD pool = 0, usage = 0;
    UINT size = 0;
    if (sync && (g_bufFix || g_profile)) {
        if (vb) { D3DVERTEXBUFFER_DESC d; ((IDirect3DVertexBuffer9*)self)->GetDesc(&d); pool = d.Pool; usage = d.Usage; size = d.Size; }
        else    { D3DINDEXBUFFER_DESC d;  ((IDirect3DIndexBuffer9*)self)->GetDesc(&d);  pool = d.Pool; usage = d.Usage; size = d.Size; }
    }
    bool whole = o == 0 && (s == 0 || s >= size);
    // A dynamic buffer rewritten from scratch can be discarded instead of waited on.
    if (sync && g_bufFix && (usage & D3DUSAGE_DYNAMIC) && whole) {
        fl |= D3DLOCK_DISCARD;
        sync = false;
    }
    double t = g_profile ? NowMs() : 0;
    HRESULT hr = ((PFN_BufLock)OrigFor(self))(self, o, s, p, fl);
    if (g_profile && !g_inFX) {
        double ms = NowMs() - t;
        if (vb) { F.vbLocks++; if (sync) F.vbLocksSync++; }
        else    { F.ibLocks++; if (sync) F.ibLocksSync++; }
        F.lockMs += ms;
        if (sync) CountCat(pool, usage, whole, ms);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkVBLock(void* self, UINT o, UINT s, void** p, DWORD fl)
{
    return LockCommon(self, o, s, p, fl, true);
}

static HRESULT STDMETHODCALLTYPE hkIBLock(void* self, UINT o, UINT s, void** p, DWORD fl)
{
    return LockCommon(self, o, s, p, fl, false);
}

// Static D3DPOOL_DEFAULT buffers are better served from MANAGED under DXVK:
// locks then write to a CPU copy instead of waiting for the GPU.
static void FixBufferPool(DWORD& usage, D3DPOOL& pool)
{
    if (g_bufFix && pool == D3DPOOL_DEFAULT && !(usage & D3DUSAGE_DYNAMIC)) {
        pool = D3DPOOL_MANAGED;
        usage &= D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING | D3DUSAGE_DONOTCLIP |
                 D3DUSAGE_POINTS | D3DUSAGE_NPATCHES;
    }
}

static HRESULT STDMETHODCALLTYPE hkTexLock(void* self, UINT l, D3DLOCKED_RECT* r, const RECT* rc, DWORD fl)
{
    double t = NowMs();
    HRESULT hr = ((PFN_TexLock)OrigFor(self))(self, l, r, rc, fl);
    if (!g_inFX) { F.texLocks++; F.texLockMs += NowMs() - t; }
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_DP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DPUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIPUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*,
                                              D3DFORMAT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CTex)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                             IDirect3DTexture9**, HANDLE*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CVB)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL,
                                            IDirect3DVertexBuffer9**, HANDLE*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CIB)(IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                            IDirect3DIndexBuffer9**, HANDLE*);
static PFN_DP oDP;
static PFN_DIP oDIP;
static PFN_DPUP oDPUP;
static PFN_DIPUP oDIPUP;
static PFN_CTex oCTex;
static PFN_CVB oCVB;
static PFN_CIB oCIB;

// Screen-space (pre-transformed) vertices = 2D. The first 2D draw after the world is the HUD.
static bool Is2D(IDirect3DDevice9* d)
{
    DWORD fvf = 0;
    d->GetFVF(&fvf);
    return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

// Weapon phase = after the game's depth clear, before the HUD.
struct SeenTex { IDirect3DBaseTexture9* t; };
static SeenTex g_seenTex[64];
static DWORD g_savedStencil[7];
static bool g_tagging;

static bool IsHandsTexture(IDirect3DDevice9* d)
{
    IDirect3DBaseTexture9* t = nullptr;
    if (FAILED(d->GetTexture(0, &t)) || !t) return false;
    bool hands = false;
    if (t->GetType() == D3DRTYPE_TEXTURE) {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(((IDirect3DTexture9*)t)->GetLevelDesc(0, &desc))) {
            hands = S.handsW && desc.Width == S.handsW && desc.Height == S.handsH;
            bool known = false;
            for (auto& e : g_seenTex) {
                if (e.t == t) { known = true; break; }
                if (!e.t) {
                    e.t = t;
                    Log("Textura del arma/mano: %ux%u formato %d niveles %u%s", desc.Width, desc.Height,
                        (int)desc.Format, t->GetLevelCount(), hands ? "  <- mano" : "");
                    known = true;
                    break;
                }
            }
        }
    }
    t->Release();
    return hands;
}

static void TagBegin(IDirect3DDevice9* d)
{
    static const D3DRENDERSTATETYPE rs[7] = { D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
                                              D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL,
                                              D3DRS_STENCILZFAIL };
    for (int i = 0; i < 7; i++) d->GetRenderState(rs[i], &g_savedStencil[i]);
    bool hands = IsHandsTexture(d);
    d->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    d->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    d->SetRenderState(D3DRS_STENCILREF, hands ? 2 : 1);
    d->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFF);
    d->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    d->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    d->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    g_tagging = true;
}

static void AfterDraw(IDirect3DDevice9* d)
{
    if (!g_tagging) return;
    static const D3DRENDERSTATETYPE rs[7] = { D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
                                              D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL,
                                              D3DRS_STENCILZFAIL };
    for (int i = 0; i < 7; i++) d->SetRenderState(rs[i], g_savedStencil[i]);
    g_tagging = false;
}

static void MenuArtBeforeDraw(IDirect3DDevice9* d);

static void BeforeDraw(IDirect3DDevice9* d)
{
    if (g_inFX) return;
    MenuArtBeforeDraw(d);
    if (g_depthDone && !g_fxDone && g_depthTex && S.enabled && S.wpn && !Is2D(d))
        TagBegin(d);
    if (Is2D(d)) {
        if (g_drawsSince3D > 0 && !g_fxDone && S.enabled) {
            g_inFX = true;
            RunFX(d, g_depthDone ? STAGE_POST : STAGE_ALL);
            g_inFX = false;
            g_fxDone = true;
            g_hudByFvf++;
        }
    } else if (!g_fxDone) {
        // world = perspective camera or the models' vertex shaders (menus use a 2D camera)
        IDirect3DVertexShader9* vs = nullptr;
        d->GetVertexShader(&vs);
        if (vs) vs->Release();
        if (vs || fabsf(g_curProjM._34) > 0.5f) g_drawsSince3D++;
    }
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_SetTransform)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
static PFN_SetTransform oSetTransform;

#include "L10n.inl"
#include "Language.inl"
#include "GameData.inl"
#include "ShadowMap.inl"
#include "Terrain.inl"
#include "HudCapture.inl"
#include "WorldCapture.inl"
#include "HudLayout.inl"
#include "Gamepad.inl"
#include "AdvancedPage.inl"
#include "WeaponIcon.inl"
#include "NewHud.inl"
#include "MenuSkin.inl"
#include "Mouse.inl"
#include "MenuArt.inl"

static HRESULT STDMETHODCALLTYPE hkDP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT n)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DP", t, n, nullptr, 0, 0, 0, 0, s);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, false, t, 0, 0, 0, s, n, nullptr, 0); AfterDraw(d); return D3D_OK; }
    if (IsGameCrosshair(d, t, s, n)) { AfterDraw(d); return D3D_OK; }
    RecordDraw(d, false, t, 0, 0, 0, s, n);
    HudLogDraw(d, "DP", t, n, nullptr, 0, s, VertsForPrims(t, n));
    WorldLogDraw(d, "DP", t, n, nullptr, 0, s);
    if (HudLayoutDraw(d, false, false, t, 0, 0, 0, s, n, nullptr, D3DFMT_UNKNOWN, nullptr, 0)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDP(d, t, s, n);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.draws++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDIP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mn, UINT nv, UINT si, UINT n)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DIP", t, n, nullptr, 0, b, mn, nv, 0);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, true, t, b, mn, nv, si, n, nullptr, 0); AfterDraw(d); return D3D_OK; }
    RecordDraw(d, true, t, b, mn, nv, si, n);
    HudLogDraw(d, "DIP", t, n, nullptr, 0, (UINT)(b + (INT)mn), nv);
    WorldLogDraw(d, "DIP", t, n, nullptr, 0, (UINT)(b + (INT)mn));
    if (AdvDraw(d, t, b, mn, nv, si, n)) return D3D_OK;
    bool dim = false, artBg = false;
    if (MenuArtDraw(d, b, mn, nv, &artBg)) {
        AfterDraw(d);
        if (artBg && SkinOn()) SkinBackgroundDim(d);
        return D3D_OK;
    }
    if (SkinDraw(d, true, t, b, mn, nv, si, n, &dim)) { AfterDraw(d); return D3D_OK; }
    if (HudLayoutDraw(d, true, false, t, b, mn, nv, si, n, nullptr, D3DFMT_UNKNOWN, nullptr, 0)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    bool terr = TerrainBegin(d);
    HRESULT hr = oDIP(d, t, b, mn, nv, si, n);
    if (terr) TerrainEnd(d);
    AfterDraw(d);
    if (dim) SkinBackgroundDim(d);
    WeaponIconDraw(d, t, b, mn, nv, si, n);
    if (!g_inFX && g_profile) { F.draws++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

// ---------------------------------------------------------------- camera capture / HUD detection

static int g_presentFrames;

static void UpdateUp(const D3DMATRIX& v)
{
    g_mainView = v;
    // world = viewPos * inverse(V); for a rigid view matrix world.y = dot(viewPos, R[1]) + w
    const int k = 1;   // Y is up in Deadhunt (the culling wedge rotates with D3DXMatrixRotationY)
    g_up[0] = v.m[k][0];
    g_up[1] = v.m[k][1];
    g_up[2] = v.m[k][2];
    g_up[3] = -(v.m[3][0] * v.m[k][0] + v.m[3][1] * v.m[k][1] + v.m[3][2] * v.m[k][2]);
}


static HRESULT STDMETHODCALLTYPE hkSetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE st, const D3DMATRIX* m)
{
    if (!g_inFX && m) {
        if (st == D3DTS_WORLD) {
            g_curWorld = *m;
        } else if (st == D3DTS_VIEW) {
            g_lastView = *m;
            if (g_curProjMain) UpdateUp(*m);
        } else if (st == D3DTS_PROJECTION) {
            g_curProjM = *m;
            bool persp = fabsf(m->_34) > 0.5f;
            if (persp && fabsf(m->_33) > 1e-6f && fabsf(1.0f - m->_33) > 1e-9f) {
                float zn = -m->_43 / m->_33, zf = m->_43 / (1.0f - m->_33);
                g_curProjMain = zf > 5000.0f;   // main camera (the weapon view uses zf = 500)
                if (!g_curProjMain && zf > 50.0f) {
                    g_wzn = zn; g_wzf = zf;
                    g_winvP11 = 1.0f / m->_11; g_winvP22 = 1.0f / m->_22;
                }
                if (g_curProjMain) {
                    g_mainProj = *m;
                    g_zn = zn; g_zf = zf;
                    g_invP11 = 1.0f / m->_11; g_invP22 = 1.0f / m->_22;
                    UpdateUp(g_lastView);
                    g_have3D = true;
                }
            } else if (!persp) {
                g_curProjMain = false;
                // Switching to 2D after the 3D world was drawn: the HUD is next.
                if (g_drawsSince3D > 0 && !g_fxDone && S.enabled) {
                    g_inFX = true;
                    RunFX(dev, g_depthDone ? STAGE_POST : STAGE_ALL);
                    g_inFX = false;
                    g_fxDone = true;
                    g_hudByProj++;
                }
            }
        }
    }
    return oSetTransform(dev, st, m);
}

// ---------------------------------------------------------------- depth texture

static void ReleaseDepth()
{
    if (g_depthSurf) { g_depthSurf->Release(); g_depthSurf = nullptr; }
    if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
}

static void CreateDepth(IDirect3DDevice9* dev)
{
    ReleaseDepth();
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    bb->Release();
    if (SUCCEEDED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_DEPTHSTENCIL, FMT_INTZ, D3DPOOL_DEFAULT,
                                     &g_depthTex, nullptr)) &&
        SUCCEEDED(g_depthTex->GetSurfaceLevel(0, &g_depthSurf))) {
        dev->SetDepthStencilSurface(g_depthSurf);
        Log("Profundidad legible (INTZ) %ux%u", d.Width, d.Height);
        return;
    }
    ReleaseDepth();
    // Fallback: plain depth buffer so the game keeps working; depth effects stay off.
    if (SUCCEEDED(dev->CreateDepthStencilSurface(d.Width, d.Height, g_depthFallbackFmt, D3DMULTISAMPLE_NONE, 0,
                                                 TRUE, &g_depthSurf, nullptr)))
        dev->SetDepthStencilSurface(g_depthSurf);
    Log("INTZ no disponible: oclusion ambiental y niebla desactivadas");
}

static HRESULT STDMETHODCALLTYPE hkDPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT n, const void* v, UINT st)
{
    BeforeDraw(d);
    CenterLogDraw(d, "DPUP", t, n, v, st, 0, 0, 0, 0);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, false, t, 0, 0, 0, 0, n, v, st); AfterDraw(d); return D3D_OK; }
    RecordDrawUP(d, false, t, 0, 0, n, nullptr, D3DFMT_UNKNOWN, v, st);
    if (!g_inFX) NoteCursor(d, n, v, st);
    HudLogDraw(d, "DPUP", t, n, v, st, 0, VertsForPrims(t, n));
    WorldLogDraw(d, "DPUP", t, n, v, st, 0);
    if (HudLayoutDraw(d, false, true, t, 0, 0, 0, 0, n, nullptr, D3DFMT_UNKNOWN, v, st)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDPUP(d, t, n, v, st);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.drawsUP++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDIPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT mn, UINT nv, UINT n,
                                         const void* i, D3DFORMAT f, const void* v, UINT st)
{
    BeforeDraw(d);
    if (g_inGameHud && !g_inFX) { HudPassDraw(d, true, t, 0, mn, nv, 0, n, v, st); AfterDraw(d); return D3D_OK; }
    RecordDrawUP(d, true, t, mn, nv, n, i, f, v, st);
    HudLogDraw(d, "DIPUP", t, n, v, st, mn, nv);
    WorldLogDraw(d, "DIPUP", t, n, v, st, mn);
    if (HudLayoutDraw(d, true, true, t, 0, mn, nv, 0, n, i, f, v, st)) return D3D_OK;
    double t0 = g_profile ? NowMs() : 0;
    HRESULT hr = oDIPUP(d, t, mn, nv, n, i, f, v, st);
    AfterDraw(d);
    if (!g_inFX && g_profile) { F.drawsUP++; F.prims += n; F.drawMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCTex(IDirect3DDevice9* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p,
                                        IDirect3DTexture9** out, HANDLE* sh)
{
    double t0 = NowMs();
    HRESULT hr = oCTex(d, w, h, l, u, f, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 19, (void*)hkTexLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCVB(IDirect3DDevice9* d, UINT len, DWORD u, DWORD fvf, D3DPOOL p,
                                       IDirect3DVertexBuffer9** out, HANDLE* sh)
{
    double t0 = NowMs();
    FixBufferPool(u, p);
    HRESULT hr = oCVB(d, len, u, fvf, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 11, (void*)hkVBLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCIB(IDirect3DDevice9* d, UINT len, DWORD u, D3DFORMAT f, D3DPOOL p,
                                       IDirect3DIndexBuffer9** out, HANDLE* sh)
{
    double t0 = NowMs();
    FixBufferPool(u, p);
    HRESULT hr = oCIB(d, len, u, f, p, out, sh);
    if (SUCCEEDED(hr) && out && *out) HookLock(*out, 11, (void*)hkIBLock);
    if (!g_inFX) { F.creates++; F.createMs += NowMs() - t0; }
    return hr;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_SetLight)(IDirect3DDevice9*, DWORD, const D3DLIGHT9*);
static PFN_SetLight oSetLight;

// The sun = the game's directional light.
static HRESULT STDMETHODCALLTYPE hkSetLight(IDirect3DDevice9* d, DWORD idx, const D3DLIGHT9* l)
{
    if (!g_inFX && l && l->Type == D3DLIGHT_DIRECTIONAL) {
        float len = sqrtf(l->Direction.x * l->Direction.x + l->Direction.y * l->Direction.y +
                          l->Direction.z * l->Direction.z);
        if (len > 1e-4f) {
            if (!g_haveSun)
                Log("Sol detectado: direccion (%.2f, %.2f, %.2f), color (%.2f, %.2f, %.2f)",
                    l->Direction.x / len, l->Direction.y / len, l->Direction.z / len,
                    l->Diffuse.r, l->Diffuse.g, l->Diffuse.b);
            g_sunDir[0] = l->Direction.x / len;
            g_sunDir[1] = l->Direction.y / len;
            g_sunDir[2] = l->Direction.z / len;
            float m = fmaxf(fmaxf(l->Diffuse.r, l->Diffuse.g), fmaxf(l->Diffuse.b, 1e-3f));
            g_sunCol[0] = l->Diffuse.r / m; g_sunCol[1] = l->Diffuse.g / m; g_sunCol[2] = l->Diffuse.b / m;
            g_haveSun = true;
        }
    }
    return oSetLight(d, idx, l);
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_Clear)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Scene)(IDirect3DDevice9*);
static PFN_Clear oClear;
static PFN_Scene oBeginScene, oEndScene;

static HRESULT STDMETHODCALLTYPE hkBeginScene(IDirect3DDevice9* d)
{
    HRESULT hr = oBeginScene(d);
    if (!g_inFX && SUCCEEDED(hr)) g_inScene = true;
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkEndScene(IDirect3DDevice9* d)
{
    if (!g_inFX) g_inScene = false;
    return oEndScene(d);
}

// The game clears depth after the world, before the weapon: last chance for AO/fog.
static HRESULT STDMETHODCALLTYPE hkClear(IDirect3DDevice9* d, DWORD n, const D3DRECT* r, DWORD flags,
                                         D3DCOLOR c, float z, DWORD st)
{
    if (!g_inFX && (flags & D3DCLEAR_ZBUFFER) && g_drawsSince3D > 0 && !g_depthDone && !g_fxDone && S.enabled) {
        g_inFX = true;
        RunFX(d, STAGE_DEPTH);
        g_inFX = false;
        g_depthDone = true;
        g_depthStages++;
    }
    if (!g_inFX && (flags & D3DCLEAR_ZBUFFER) && g_depthTex && S.wpn) {
        flags |= D3DCLEAR_STENCIL;
        st = 0;
    }
    return oClear(d, n, r, flags, c, z, st);
}

static void InstallHooks(IDirect3DDevice9* dev)
{
    if (void* o = HookVtable(dev, 41, (void*)hkBeginScene)) oBeginScene = (PFN_Scene)o;
    if (void* o = HookVtable(dev, 42, (void*)hkEndScene))   oEndScene = (PFN_Scene)o;
    if (void* o = HookVtable(dev, 43, (void*)hkClear))      oClear = (PFN_Clear)o;
    if (void* o = HookVtable(dev, 51, (void*)hkSetLight))   oSetLight = (PFN_SetLight)o;
    if (void* o = HookVtable(dev, 91, (void*)hkCreateVS))   oCreateVS = (PFN_CreateVS)o;
    if (void* o = HookVtable(dev, 94, (void*)hkSetVSConstF)) oSetVSConstF = (PFN_SetVSConstF)o;
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    g_qpcMs = 1000.0 / (double)fq.QuadPart;
    if (g_bufFix || g_profile) {
        if (void* o = HookVtable(dev, 26, (void*)hkCVB)) oCVB = (PFN_CVB)o;
        if (void* o = HookVtable(dev, 27, (void*)hkCIB)) oCIB = (PFN_CIB)o;
    }
    if (void* o = HookVtable(dev, 81, (void*)hkDP))  oDP = (PFN_DP)o;
    if (void* o = HookVtable(dev, 82, (void*)hkDIP)) oDIP = (PFN_DIP)o;
    if (void* o = HookVtable(dev, 44, (void*)hkSetTransform)) oSetTransform = (PFN_SetTransform)o;
    if (void* o = HookVtable(dev, 83, (void*)hkDPUP))  oDPUP = (PFN_DPUP)o;
    if (void* o = HookVtable(dev, 84, (void*)hkDIPUP)) oDIPUP = (PFN_DIPUP)o;
    if (g_profile) {
        if (void* o = HookVtable(dev, 23, (void*)hkCTex))  oCTex = (PFN_CTex)o;
        Log("Perfilador activo: se registran los fotogramas lentos");
    }
    if (g_bufFix) Log("Arreglo de buffers activo");
}

static void LogCats()
{
    static const char* pools[] = { "DEFAULT", "MANAGED", "SYSMEM", "SCRATCH" };
    char line[512] = "";
    for (auto& c : g_cats) {
        if (!c.count) continue;
        char part[96];
        sprintf_s(part, " [%s%s%s %s x%d %.1fms]", c.pool < 4 ? pools[c.pool] : "?",
                  (c.usage & D3DUSAGE_DYNAMIC) ? " DYN" : "", (c.usage & D3DUSAGE_WRITEONLY) ? " WO" : "",
                  c.whole ? "entero" : "parcial", c.count, c.ms);
        strcat_s(line, part);
    }
    if (line[0]) Log("   esperas por tipo:%s", line);
}

// Called at the start of Present (before our FX) and after the real Present.
static double g_presentEnd, g_frameStart, g_avgFrame;
static double g_sumFrame, g_maxFrame, g_sumGame;
static int g_frames, g_slowFrames;

static void ProfileFrame(double presentStart, double fxMs, double presentMs)
{
    if (g_frameStart > 0) {
        double frame = presentStart - g_frameStart;
        double game = presentStart - g_presentEnd;   // time outside Present = game + driver work
        double d3d = F.drawMs + F.lockMs + F.texLockMs + F.createMs;
        g_avgFrame = g_avgFrame ? g_avgFrame * 0.98 + frame * 0.02 : frame;
        g_sumFrame += frame; g_sumGame += game; g_frames++;
        if (frame > g_maxFrame) g_maxFrame = frame;
        if (frame > 12.0 && frame > g_avgFrame * 1.8) {
            g_slowFrames++;
            Log("LENTO %.1fms (media %.1f) | juego %.1f (de ello D3D %.1f: draw %.1f, lock %.1f, tex %.1f, crear %.1f)"
                " | FX %.1f | Present %.1f | draws %d, UP %d, prims %d | VBlock %d (sync %d) IBlock %d (sync %d)"
                " texlock %d | creados %d",
                frame, g_avgFrame, game, d3d, F.drawMs, F.lockMs, F.texLockMs, F.createMs, fxMs, presentMs,
                F.draws, F.drawsUP, F.prims, F.vbLocks, F.vbLocksSync, F.ibLocks, F.ibLocksSync, F.texLocks,
                F.creates);
            LogCats();
        }
        if (g_frames >= 600) {
            Log("RESUMEN %d fotogramas: media %.2fms (juego %.2fms), maximo %.1fms, lentos %d",
                g_frames, g_sumFrame / g_frames, g_sumGame / g_frames, g_maxFrame, g_slowFrames);
            g_sumFrame = g_sumGame = g_maxFrame = 0;
            g_frames = g_slowFrames = 0;
        }
    }
    g_frameStart = presentStart;
    g_presentEnd = presentStart + fxMs + presentMs;
    memset(&F, 0, sizeof(F));
    memset(g_cats, 0, sizeof(g_cats));
}

static HRESULT STDMETHODCALLTYPE hkPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND c, const RGNDATA* d)
{
    double tStart = g_profile ? NowMs() : 0;
    HRESULT hr = 0;
    static bool tWas, rWas;
    AdvFrame();
    MouseLiveFrame();
    WeaponIconFrame(dev);
    if (KeyPressed(S.toggleKey, tWas)) {
        S.enabled = !S.enabled;
        if (S.enabled && !g_shadersOk) BuildShaders(dev);
    }
    if (KeyPressed(S.reloadKey, rWas)) {
        bool en = S.enabled;
        LoadSettings();
        TerrainLoadSettings();
        GamepadLoadSettings();
        S.enabled = en || S.enabled;
        BuildShaders(dev);
        Log("Recargado DeadhuntFX.ini y DeadhuntFX.hlsl");
    }
    double tFx = g_profile ? NowMs() : 0;
    if (S.enabled && !g_fxDone) {
        g_inFX = true;
        RunFX(dev, g_depthDone ? STAGE_POST : STAGE_ALL);
        g_inFX = false;
    }
    if (++g_presentFrames % 600 == 0) {
        Log("600 fotogramas: AO/niebla antes de borrar profundidad %d | efectos antes del HUD %d "
            "(por vertices 2D %d, por camara 2D %d) | camara 3D %s",
            g_depthStages, g_hudByFvf + g_hudByProj, g_hudByFvf, g_hudByProj, g_have3D ? "si" : "no");
        if (!g_haveSun) Log("   (sin luz direccional: los rayos de sol no se pueden calcular)");
        Log("   sombras: %d grabados (sin shader %d, paisaje %d, animados %d; desde memoria %d), %d dibujados "
            "desde el sol, %d omitidos, %d sin escritura de profundidad, %d con transparencia (ultimo fotograma)",
            g_sdCount, g_sdKind[0], g_sdKind[1], g_sdKind[2], g_sdUP, g_smDrawn, g_sdSkipped, g_sdNoZ, g_sdBlend);
        g_hudByFvf = g_hudByProj = g_depthStages = 0;
    }
    ReleaseShadowDraws();
    g_sdSkipped = g_sdNoZ = g_sdBlend = 0;
    g_sdKind[0] = g_sdKind[1] = g_sdKind[2] = 0;
    g_sdUP = 0;
    g_frameHad3D = g_drawsSince3D > 0;
    TerrainFrame(g_frameHad3D);
    g_fxDone = false;
    g_depthDone = false;
    g_worldZValid = false;
    g_drawsSince3D = 0;
    double tPresent = g_profile ? NowMs() : 0;
    MenuArtFrame(dev, g_frameHad3D);
    CreditClickFrame();
    MenuSkinFrame(dev);
    CenterLogFrame();
    if (g_hudCapture && g_shotPending) { SaveScreenshot(dev); g_shotPending = false; }
    hr = oPresent(dev, a, b, c, d);
    HudCaptureFrame();
    TextFrameEnd();
    PanelFrameEnd();
    if (g_profile)
        ProfileFrame(tStart, tPresent - tFx, NowMs() - tPresent);
    return hr;
}

// Windowed mode at (or above) the monitor size: a borderless window covering the monitor,
// instead of a bordered window that does not fit (Windows shrinks it and the image gets scaled).
// Fullscreen refresh rate: [Display] RefreshRate from the AVANZADO tab (0 = the highest the
// monitor offers at this resolution). Overrides the old fixed 144 Hz exe patch.
static void ApplyRefresh(D3DPRESENT_PARAMETERS* pp)
{
    if (!pp || pp->Windowed) return;
    int want = (int)IniF(L"Display", L"RefreshRate", 0), best = 0, exact = 0;
    DEVMODEW dm = { };
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
        if (dm.dmPelsWidth != pp->BackBufferWidth || dm.dmPelsHeight != pp->BackBufferHeight) continue;
        int hz = (int)dm.dmDisplayFrequency;
        if (hz > best) best = hz;
        if (hz == want) exact = hz;
    }
    int hz = exact ? exact : best;
    if (hz) {
        pp->FullScreen_RefreshRateInHz = hz;
        Log("Pantalla completa a %d Hz", hz);
    }
}

static RECT g_bdlRect;
static bool g_bdlActive;
static WNDPROC g_bdlOldProc;

// The game moves/resizes its window back to the image size; keep the borderless rectangle.
static LRESULT CALLBACK BorderlessProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_WINDOWPOSCHANGING && g_bdlActive && !IsIconic(h)) {
        WINDOWPOS* wpos = (WINDOWPOS*)lp;
        wpos->x = g_bdlRect.left;
        wpos->y = g_bdlRect.top;
        wpos->cx = g_bdlRect.right - g_bdlRect.left;
        wpos->cy = g_bdlRect.bottom - g_bdlRect.top;
        wpos->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
    }
    return CallWindowProcW(g_bdlOldProc, h, msg, wp, lp);
}

static void MakeBorderless(HWND wnd, const D3DPRESENT_PARAMETERS* pp)
{
    if (!wnd || !pp || !pp->Windowed || IniF(L"General", L"Borderless", 1) == 0) return;
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
    RECT r = mi.rcMonitor;
    int mw = r.right - r.left, mh = r.bottom - r.top;
    int bw = (int)pp->BackBufferWidth, bh = (int)pp->BackBufferHeight;
    if (bw < mw && bh < mh) { g_bdlActive = false; return; }
    // bigger than the monitor (e.g. an ultrawide resolution on a 16:9 screen): shrink it with
    // its real proportions, centered - a faithful preview of the wide screen
    float k = fminf(1.0f, fminf((float)mw / bw, (float)mh / bh));
    int ww = (int)(bw * k + 0.5f), wh = (int)(bh * k + 0.5f);
    LONG st = GetWindowLongW(wnd, GWL_STYLE);
    SetWindowLongW(wnd, GWL_STYLE, (st & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP | WS_VISIBLE);
    SetWindowLongW(wnd, GWL_EXSTYLE, GetWindowLongW(wnd, GWL_EXSTYLE) & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME));
    g_bdlRect = { r.left + (mw - ww) / 2, r.top + (mh - wh) / 2, r.left + (mw - ww) / 2 + ww, r.top + (mh - wh) / 2 + wh };
    g_bdlActive = true;
    if (!g_bdlOldProc) g_bdlOldProc = (WNDPROC)SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)BorderlessProc);
    SetWindowPos(wnd, HWND_TOP, g_bdlRect.left, g_bdlRect.top, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    Log("Ventana sin bordes %dx%d (imagen %dx%d)", ww, wh, bw, bh);
}

static HRESULT STDMETHODCALLTYPE hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    ReleaseResources();   // D3DPOOL_DEFAULT resources must go before Reset
    ReleaseDepth();
    ReleaseShadowDraws();
    // the HUD state block holds the textures bound when it was captured (our render targets / depth
    // among them): DXVK refuses to reset while any of them is alive
    if (g_hudSB) { g_hudSB->Release(); g_hudSB = nullptr; }
    BOOL autoDS = pp->EnableAutoDepthStencil;
    if (g_depthWanted) pp->EnableAutoDepthStencil = FALSE;
    ApplyRefresh(pp);
    HRESULT hr = oReset(dev, pp);
    if (FAILED(hr)) Log("Reset fallido (0x%08X) a %ux%u", (unsigned)hr, pp->BackBufferWidth, pp->BackBufferHeight);
    pp->EnableAutoDepthStencil = autoDS;
    if (SUCCEEDED(hr) && g_depthWanted) CreateDepth(dev);
    if (SUCCEEDED(hr)) MakeBorderless(pp->hDeviceWindow ? pp->hDeviceWindow : g_hwnd, pp);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkCreateDevice(IDirect3D9* d3d, UINT ad, D3DDEVTYPE type, HWND wnd, DWORD flags,
                                                D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    // Depth effects need a readable depth buffer (INTZ), which cannot be multisampled.
    bool wantDepth = (IniF(L"AO", L"Enabled", 1) != 0 || IniF(L"Fog", L"Enabled", 1) != 0)
                     && pp->EnableAutoDepthStencil && pp->MultiSampleType == D3DMULTISAMPLE_NONE;
    BOOL autoDS = pp->EnableAutoDepthStencil;
    if (wantDepth) {
        g_depthFallbackFmt = pp->AutoDepthStencilFormat;
        pp->EnableAutoDepthStencil = FALSE;
    }
    ApplyRefresh(pp);
    HRESULT hr = oCreateDevice(d3d, ad, type, wnd, flags, pp, out);
    pp->EnableAutoDepthStencil = autoDS;
    if (SUCCEEDED(hr) && out && *out && wantDepth) {
        g_depthWanted = true;
        CreateDepth(*out);
    }
    if (SUCCEEDED(hr) && out && *out) {
        g_hwnd = wnd;
        MakeBorderless(pp->hDeviceWindow ? pp->hDeviceWindow : wnd, pp);
        if (void* o = HookVtable(*out, 17, (void*)hkPresent)) oPresent = (PFN_Present)o;
        if (void* o = HookVtable(*out, 16, (void*)hkReset))   oReset = (PFN_Reset)o;
        g_profile = IniF(L"Debug", L"Profile", 0) != 0;
        g_bufFix = IniF(L"Buffers", L"Fix", 1) != 0;
        InstallHooks(*out);
        MenuArtStartup(*out, pp->hDeviceWindow ? pp->hDeviceWindow : wnd, pp);
        Log("Dispositivo creado %ux%u, MSAA %d", pp->BackBufferWidth, pp->BackBufferHeight, pp->MultiSampleType);
    } else {
        Log("CreateDevice fallo: 0x%08X", (unsigned)hr);
    }
    return hr;
}

// ---------------------------------------------------------------- exports

typedef IDirect3D9*(WINAPI* PFN_Create9)(UINT);
static PFN_Create9 pRealCreate9;

static bool LoadReal()
{
    if (pRealCreate9) return true;
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sdxvk_d3d9.dll", g_dir);
    HMODULE m = LoadLibraryW(path);
    if (!m) {
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        swprintf_s(path, L"%s\\d3d9.dll", sys);
        m = LoadLibraryW(path);
    }
    if (m) pRealCreate9 = (PFN_Create9)GetProcAddress(m, "Direct3DCreate9");
    Log("d3d9 real: %ls", pRealCreate9 ? path : L"(no encontrado)");
    return pRealCreate9 != nullptr;
}

// ---------------------------------------------------------------- display mode list
// Options only lists the monitor's own modes; a saved resolution missing from that list
// (ultrawide ini on a 16:9 monitor) breaks the Resolution control. In windowed mode any size
// works, so add the configured size plus common ultrawide sizes to the list the game sees.

typedef UINT(STDMETHODCALLTYPE* PFN_ModeCount)(IDirect3D9*, UINT, D3DFORMAT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_EnumModes)(IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);
static PFN_ModeCount oModeCount;
static PFN_EnumModes oEnumModes;
static std::vector<D3DDISPLAYMODE> g_modes[2];   // [0] X8R8G8B8, [1] R5G6B5
static D3DFORMAT g_modeFmt[2] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5 };

static std::vector<D3DDISPLAYMODE>* ModeList(IDirect3D9* d3d, UINT ad, D3DFORMAT fmt)
{
    int k = fmt == g_modeFmt[0] ? 0 : fmt == g_modeFmt[1] ? 1 : -1;
    if (k < 0 || ad != 0) return nullptr;
    std::vector<D3DDISPLAYMODE>& v = g_modes[k];
    if (!v.empty()) return &v;
    UINT n = oModeCount(d3d, ad, fmt), maxHz = 60;
    for (UINT i = 0; i < n; i++) {
        D3DDISPLAYMODE m;
        if (SUCCEEDED(oEnumModes(d3d, ad, fmt, i, &m))) { v.push_back(m); maxHz = max(maxHz, m.RefreshRate); }
    }
    if (v.empty()) return &v;
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%sGtEngine.ini", g_dir);
    UINT extra[][2] = { { (UINT)GetPrivateProfileIntW(L"GRAPHICS", L"BackBufferWidth", 0, ini),
                          (UINT)GetPrivateProfileIntW(L"GRAPHICS", L"BackBufferHeight", 0, ini) },
                        { 2560, 1080 }, { 3440, 1440 }, { 3840, 1600 } };
    bool windowed = GetPrivateProfileIntW(L"GRAPHICS", L"FullScreen", 1, ini) == 0;
    for (int e = 0; e < (windowed ? 4 : 1); e++) {
        UINT w = extra[e][0], h = extra[e][1];
        if (w < 640 || h < 480) continue;
        bool have = false;
        for (auto& m : v) have |= m.Width == w && m.Height == h;
        if (!have) {
            v.push_back({ w, h, maxHz, fmt });
            Log("Resolucion %ux%u anadida a la lista de modos", w, h);
        }
    }
    std::stable_sort(v.begin(), v.end(), [](const D3DDISPLAYMODE& a, const D3DDISPLAYMODE& b) {
        return a.Width != b.Width ? a.Width < b.Width : a.Height != b.Height ? a.Height < b.Height : a.RefreshRate < b.RefreshRate;
    });
    return &v;
}

static UINT STDMETHODCALLTYPE hkModeCount(IDirect3D9* d3d, UINT ad, D3DFORMAT fmt)
{
    if (auto* v = ModeList(d3d, ad, fmt)) return (UINT)v->size();
    return oModeCount(d3d, ad, fmt);
}

static HRESULT STDMETHODCALLTYPE hkEnumModes(IDirect3D9* d3d, UINT ad, D3DFORMAT fmt, UINT i, D3DDISPLAYMODE* m)
{
    if (auto* v = ModeList(d3d, ad, fmt)) {
        if (i >= v->size() || !m) return D3DERR_INVALIDCALL;
        *m = (*v)[i];
        return D3D_OK;
    }
    return oEnumModes(d3d, ad, fmt, i, m);
}

extern "C" IDirect3D9* WINAPI FX_Direct3DCreate9(UINT sdk)
{
    if (!LoadReal()) return nullptr;
    IDirect3D9* d3d = pRealCreate9(sdk);
    if (d3d)
    {
        if (void* o = HookVtable(d3d, 16, (void*)hkCreateDevice)) oCreateDevice = (PFN_CreateDevice)o;
        if (void* o = HookVtable(d3d, 6, (void*)hkModeCount))    oModeCount = (PFN_ModeCount)o;
        if (void* o = HookVtable(d3d, 7, (void*)hkEnumModes))    oEnumModes = (PFN_EnumModes)o;
    }
    return d3d;
}

// ---------------------------------------------------------------- engine fatal errors
// The engine quits silently on a fatal error (0x40cae0, message in [0x6b71c4]+0x398).
// Log the message and who called it, so crashes like "Options closes the game" are traceable.

static void __stdcall LogFatal(DWORD ret, DWORD callerEbp)
{
    const char* msg = "?";
    DWORD eng = *(DWORD*)0x6b71c4;
    if (eng && !IsBadReadPtr((void*)(eng + 0x398), 4)) msg = (const char*)(eng + 0x398);
    DWORD ret2 = 0;
    if (callerEbp && !IsBadReadPtr((void*)(callerEbp + 4), 4)) ret2 = *(DWORD*)(callerEbp + 4);
    Log("ERROR FATAL del motor: \"%.300s\" (llamado desde %08X, %08X)", msg, ret, ret2);
}

static __declspec(naked) void FatalStub()
{
    __asm {
        pushad
        push ebp
        push dword ptr [esp + 36]
        call LogFatal
        popad
        push ebp
        mov ebp, esp
        sub esp, 0x10
        push 0x40cae6
        ret
    }
}

static void HookEngineFatal()
{
    unsigned char* p = (unsigned char*)0x40cae0;
    static const unsigned char orig[6] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x10 };
    if (IsBadReadPtr(p, 6) || memcmp(p, orig, 6)) return;
    DWORD old;
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)FatalStub - (DWORD)(p + 5);
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
}

// Other silent exits: log ExitProcess callers and unexpected exceptions.
static void LogBacktrace(const char* what, DWORD ret)
{
    char buf[256];
    int n = sprintf_s(buf, "%s desde %08X", what, ret);
    DWORD* fp;
    __asm mov fp, ebp
    for (int i = 0; i < 8 && fp && !IsBadReadPtr(fp, 8); i++) {
        n += sprintf_s(buf + n, sizeof(buf) - n, " <- %08X", fp[1]);
        fp = (DWORD*)fp[0];
    }
    Log("%s", buf);
}

static void (WINAPI* oExitProcess)(UINT);
static void WINAPI hkExitProcess(UINT code)
{
    LogBacktrace("ExitProcess", (DWORD)_ReturnAddress());
    oExitProcess(code);
}

static LONG WINAPI VehLog(EXCEPTION_POINTERS* e)
{
    static int n;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if ((c & 0xF0000000) == 0xC0000000 && n++ < 20)
    {
        char buf[256];
        int k = sprintf_s(buf, "Excepcion %08X en %08X", c, (DWORD)e->ExceptionRecord->ExceptionAddress);
        DWORD* fp = (DWORD*)e->ContextRecord->Ebp;
        for (int i = 0; i < 8 && fp && !IsBadReadPtr(fp, 8); i++) {
            k += sprintf_s(buf + k, sizeof(buf) - k, " <- %08X", fp[1]);
            fp = (DWORD*)fp[0];
        }
        Log("%s", buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void HookExitPaths()
{
    AddVectoredExceptionHandler(1, VehLog);
    unsigned char* base = (unsigned char*)GetModuleHandleW(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    void* target = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess");
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++)
        for (void** f = (void**)(base + d->FirstThunk); *f; f++)
            if (*f == target) {
                DWORD old;
                VirtualProtect(f, 4, PAGE_READWRITE, &old);
                oExitProcess = (void (WINAPI*)(UINT))*f;
                *f = (void*)hkExitProcess;
                VirtualProtect(f, 4, old, &old);
            }
}

// Engine text renderer hook (0x494930): the AVANZADO tab draws through it and hides the
// controls page texts; [Debug] TextProbe=1 also logs every text draw during an F11 capture.
static bool g_textProbe;
static int __stdcall OnTextDraw(DWORD ret, void* font, DWORD* a)
{
    static int hudTexts;
    if (g_textProbe && ret > 0x4c7000 && ret < 0x4c8b00 && hudTexts < 60) {
        hudTexts++;
        const char* str = (const char*)a[6];
        Log("texto HUD: '%.20s' tam %d desde %08X", IsBadReadPtr(str, 1) ? "?" : str, (int)a[0], ret);
    }
    if (g_textProbe && g_hudCapture) {
        const char* str = (const char*)a[6];
        Log("texto(pag %d): tam %d x %.1f y %.1f | %08X %08X %08X %08X | '%.40s' desde %08X", OptionsPage(),
            (int)a[0], *(float*)&a[1], *(float*)&a[2], a[3], a[4], a[5], a[7],
            IsBadReadPtr(str, 1) ? "?" : str, ret);
    }
    if (HudPassText(a)) return 1;
    if (SkinOn()) {
        if (AdvOnText(font, a)) return 1;      // AVANZADO logic first (hides the controls page, recolours tabs)
        return SkinText((int)a[0], *(float*)&a[1], *(float*)&a[2], *(float*)&a[3], *(float*)&a[4], a[5],
                        (const char*)a[6], a[7]) ? 1 : 0;
    }
    return AdvOnText(font, a) ? 1 : 0;
}

static __declspec(naked) void TextStub()
{
    __asm {
        pushad
        lea eax, [esp + 36]
        push eax
        push ecx
        push dword ptr [esp + 40]
        call OnTextDraw
        test eax, eax
        popad
        jnz skip
        push ebp
        mov ebp, esp
        sub esp, 0x2c0
        push 0x494939
        ret
    skip:
        xor eax, eax
        ret 0x20
    }
}

static void HookText()
{
    unsigned char* p = (unsigned char*)0x494930;
    static const unsigned char orig[9] = { 0x55, 0x8b, 0xec, 0x81, 0xec, 0xc0, 0x02, 0x00, 0x00 };
    g_textProbe = IniF(L"Debug", L"TextProbe", 0) != 0;
    if (IsBadReadPtr(p, 9) || memcmp(p, orig, 9) || (!S.advEnabled && !g_textProbe)) return;
    DWORD old;
    VirtualProtect(p, 9, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)TextStub - (DWORD)(p + 5);
    memset(p + 5, 0x90, 4);
    VirtualProtect(p, 9, old, &old);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // restarted from AVANZADO: wait until the previous copy has fully exited
        wchar_t wpid[16];
        if (GetEnvironmentVariableW(L"DHFX_WAIT_PID", wpid, 16)) {
            SetEnvironmentVariableW(L"DHFX_WAIT_PID", nullptr);
            if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)_wtoi(wpid))) {
                WaitForSingleObject(h, 15000);
                CloseHandle(h);
            }
        }
        GetModuleFileNameW(nullptr, g_dir, MAX_PATH);
        if (wchar_t* s = wcsrchr(g_dir, L'\\')) s[1] = 0;
        LoadSettings();
        TerrainLoadSettings();
        GamepadLoadSettings();
        ApplyExePatch();
        ApplyLanguage();
        HookGameData();
        HookEngineFatal();
        HookExitPaths();
        HookText();
        HookDirectInput();
        HookHudDraw();
        HookMouseSlider();
    }
    return TRUE;
}
