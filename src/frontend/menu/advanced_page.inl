// DeadhuntFX - "AVANZADO": a fourth tab in the game's Options screen, drawn by this module.
//
// The game's Options are three separate windows (graphics / controls / sound) owned by the GUI
// manager [0x6b71ec] (+0x120 / +0x124 / +0x128); the visible one is at +0x80. Each window has
// its own frame mesh with a notch at its tab. AVANZADO lives on top of the CONTROLES window:
//  - the tab label is drawn with the game's own text renderer (0x494930) next to the others,
//  - the CONTROLES frame is redrawn with its notch moved one tab down,
//  - everything inside the controls page is hidden and its mouse clicks are swallowed,
//  - the page texts use the game's renderer and the sliders reuse the controls page slider mesh.
// Included from main.cpp after hud_layout.inl. Texts are cp1252 (the translated fonts).

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

// ---------------------------------------------------------------- engine access

static const DWORD ENG_GUI = 0x6b71ec, ENG_CFG = 0x6b71c8;
static const DWORD CFG_FULLSCREEN = 0x258, CFG_FOVCOEF = 0x2c4, CFG_SCANANGLE = 0x2c8;

enum { PAGE_NONE, PAGE_GFX, PAGE_CTRL, PAGE_SND };

static int OptionsPage()
{
    DWORD m = *(DWORD*)ENG_GUI;
    if (!m || IsBadReadPtr((void*)m, 0x130)) return PAGE_NONE;
    DWORD top = *(DWORD*)(m + 0x80);                 // the window on screen
    if (!top) return PAGE_NONE;
    if (top == *(DWORD*)(m + 0x120)) return PAGE_GFX;
    if (top == *(DWORD*)(m + 0x124)) return PAGE_CTRL;
    if (top == *(DWORD*)(m + 0x128)) return PAGE_SND;
    return PAGE_NONE;
}

static float* CfgF(DWORD off)
{
    DWORD c = *(DWORD*)ENG_CFG;
    return c && !IsBadWritePtr((void*)(c + off), 4) ? (float*)(c + off) : nullptr;
}

static int* CfgI(DWORD off) { return (int*)CfgF(off); }

// Original text renderer (thiscall, 8 stack args, callee cleanup): called as fastcall with a dummy edx.
typedef int(__fastcall* PFN_EngText)(void* font, void* edx, int size, float x1, float y1, float x2, float y2,
                                      DWORD color, const char* text, DWORD flags);
enum { TXT_LEFT = 0, TXT_CENTER = 1, TXT_RIGHT = 0x10 };

static __declspec(naked) void EngTextOrig()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x2c0
        push 0x494939
        ret
    }
}

// ---------------------------------------------------------------- page model

struct AdvVals {
    float fov;          // degrees, measured at 16:9 (Cambiar_FOV.bat convention)
    int   mode;         // 0 borderless window, 1 fullscreen, 2 window
    int   hz;           // fullscreen refresh rate, 0 = highest available
    int   maxFps;       // 0 = unlimited
    int   lang;         // LANG_* (applies on restart)
    bool  vsync;
};

static bool    g_advOn, g_advApplied;
static int     g_advSection;                 // 0 PANTALLA, 1 EFECTOS, 2 TERRENO
static int     g_advSwitchWait;              // frames left while switching the game to CONTROLES
static bool    g_advSwitchNow;
static AdvVals g_av, g_avSaved;
static Settings g_advSavedS;
static decltype(T) g_advSavedT;
static float   g_advSavedCoef, g_advSavedScan;
static std::vector<int> g_advRates;          // monitor refresh rates for the configured resolution
static const int kFpsChoices[] = { 0, 60, 120, 144, 165, 240 };

// geometry captured from the game's own tab texts (screen pixels)
static bool  g_advTabsSeen;
static float g_tabCtrl[4], g_tabSnd[4];      // text rects of CONTROLES and SONIDO
static void* g_tabFont;
static int   g_tabSize;
static DWORD g_colTab = 0xCCAAAA99, g_colTabSel = 0xFFFBF2BD, g_colHover = 0xFFFFFFFF;
static const DWORD COL_LABEL = 0xFF93C3C4, COL_VALUE = 0xFFFBF2BD, COL_DIM = 0xCC93C3C4;

// clickable regions of the current frame
struct AdvHit { float r[4]; int id; };
enum { HIT_TAB = 1, HIT_CTRLTAB, HIT_SEC0, HIT_SEC1, HIT_SEC2, HIT_APPLY, HIT_DEFAULTS, HIT_RESTART, HIT_LATER, HIT_ROW = 100, HIT_SLIDER = 200 };
static AdvHit g_advHits[64];
static int    g_advHitN;
static int    g_advDrag = -1;                // slider row being dragged

struct AdvSlider { float r[4]; float v; bool used; };
static AdvSlider g_advSliders[16];          // target rects for the slider artwork this frame

static float UX(float u) { return u * g_w / 800.0f; }
static float VY(float v) { return v * g_h / 600.0f; }

static bool InRect(const float* r, float x, float y) { return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]; }

static void AddHit(float x1, float y1, float x2, float y2, int id)
{
    if (g_advHitN < 64) g_advHits[g_advHitN++] = { { x1, y1, x2, y2 }, id };
}

static int HitAt(float x, float y)
{
    for (int i = g_advHitN - 1; i >= 0; i--)
        if (InRect(g_advHits[i].r, x, y)) return g_advHits[i].id;
    return 0;
}

// ---- rows

enum RowKind { RK_TOGGLE, RK_CHOICE, RK_SLIDER };
struct AdvRow { int label; RowKind kind; bool restart; };   // label: Str id (l10n_strings.inl)

static const AdvRow kRowsScreen[] = {
    { R_FOV, RK_SLIDER, false },
    { R_MODE, RK_CHOICE, true },
    { R_HZ, RK_CHOICE, true },
    { R_FPS, RK_CHOICE, true },
    { R_VSYNC, RK_TOGGLE, true },
    { R_HUDSIZE, RK_SLIDER, false },
    { R_HUDAREA, RK_TOGGLE, false },
    { R_LANG, RK_CHOICE, true },
};
static const AdvRow kRowsFx[] = {
    { R_SMAA, RK_TOGGLE, false },
    { R_SHARP, RK_SLIDER, false },
    { R_BLOOM, RK_SLIDER, false },
    { R_AO, RK_SLIDER, false },
    { R_FOG, RK_SLIDER, false },
    { R_RAYS, RK_SLIDER, false },
    { R_DOF, RK_SLIDER, false },
    { R_SHADOWS, RK_SLIDER, false },
    { R_WPNLIGHT, RK_TOGGLE, false },
    { R_FLASH, RK_SLIDER, false },
    { R_COLOR, RK_SLIDER, false },
};

static const AdvRow kRowsTerrain[] = {
    { R_TERRAIN, RK_TOGGLE, false },
    { R_PARALLAX, RK_SLIDER, false },
    { R_RELIEF, RK_SLIDER, false },
    { R_ANTITILE, RK_SLIDER, false },
    { R_GRASS, RK_TOGGLE, false },
    { R_OBJRELIEF, RK_SLIDER, false },
};

static int RowCount() { return g_advSection == 0 ? 8 : g_advSection == 1 ? 11 : 6; }
static const AdvRow& RowDef(int i) { return g_advSection == 0 ? kRowsScreen[i] : g_advSection == 1 ? kRowsFx[i] : kRowsTerrain[i]; }

// terrain rows (terrain.inl): value pointer and slider maximum
static float* TerrValue(int row, float* hi)
{
    switch (row) {
    case 1: *hi = 20;   return &T.parallax;
    case 2: *hi = 1.2f; return &T.relief;
    case 3: *hi = 1;    return &T.antiTile;
    case 5: *hi = 1.5f; return &T.objRelief;
    }
    *hi = 1;
    return nullptr;
}

// slider ranges: value = lo + t * (hi - lo)
static void SliderRange(int row, float& lo, float& hi)
{
    if (g_advSection == 0) {
        if (row == 0) { lo = 60; hi = 110; } else { lo = 0.6f; hi = 1.4f; }
        return;
    }
    if (g_advSection == 2) { lo = 0; TerrValue(row, &hi); return; }
    static const float fxHi[11] = { 1, 1, 1, 2, 1, 1.5f, 1, 1, 1, 3, 0.6f };
    lo = 0; hi = fxHi[row];
}

static float* FxValue(int row, bool** en)
{
    static bool dummy;
    *en = &dummy;
    switch (row) {
    case 1:  *en = &S.sharpen; return &S.sharpenStrength;
    case 2:  *en = &S.bloom;   return &S.bloomIntensity;
    case 3:  *en = &S.ao;      return &S.aoIntensity;
    case 4:  *en = &S.fog;     return &S.fogMax;
    case 5:  *en = &S.rays;    return &S.raysIntensity;
    case 6:  *en = &S.dof;     return &S.dofStrength;
    case 7:  *en = &S.shadows; return &S.shStrength;
    case 9:  *en = &S.flash;   return &S.flashStrength;
    case 10: *en = &S.color;   return &S.vibrance;
    }
    return nullptr;
}

static float SliderGet(int row)    // 0..1
{
    float lo, hi;
    SliderRange(row, lo, hi);
    float v;
    if (g_advSection == 0) v = row == 0 ? g_av.fov : S.hudScale;
    else if (g_advSection == 2) {
        float h;
        float* p = TerrValue(row, &h);
        v = p ? *p : 0;
        if (row == 5 && !T.objects) v = 0;
    } else {
        bool* en;
        float* p = FxValue(row, &en);
        v = p && *en ? *p : 0;
    }
    return fminf(1, fmaxf(0, (v - lo) / (hi - lo)));
}

static void ApplyFovLive();

static void SliderSet(int row, float t)
{
    float lo, hi;
    SliderRange(row, lo, hi);
    t = fminf(1, fmaxf(0, t));
    float v = lo + t * (hi - lo);
    if (g_advSection == 0) {
        if (row == 0) { g_av.fov = floorf(v + 0.5f); ApplyFovLive(); }
        else S.hudScale = floorf(v * 20 + 0.5f) / 20;
        return;
    }
    if (g_advSection == 2) {
        float h;
        float* p = TerrValue(row, &h);
        if (!p) return;
        *p = t < 0.02f ? 0 : floorf(v * 100 + 0.5f) / 100;
        if (row == 5) T.objects = t >= 0.02f;
        return;
    }
    bool* en;
    float* p = FxValue(row, &en);
    if (!p) return;
    if (t < 0.02f) { *en = false; return; }
    *en = true;
    *p = floorf(v * 100 + 0.5f) / 100;
    if (row == 10) S.vibrance = *p;
}

static const char* RowValueText(int row, char* buf, size_t n)
{
    const AdvRow& r = RowDef(row);
    if (r.kind == RK_SLIDER) {
        if (g_advSection == 0 && row == 0) { sprintf_s(buf, n, "%.0f\xB0", g_av.fov); return buf; }
        sprintf_s(buf, n, "%.0f%%", SliderGet(row) * 100);
        if (g_advSection == 0 && row == 5) sprintf_s(buf, n, "%.0f%%", S.hudScale * 100);
        if (SliderGet(row) < 0.02f && g_advSection >= 1) strcpy_s(buf, n, Tr(V_NO));
        return buf;
    }
    if (g_advSection == 0) {
        switch (row) {
        case 1: { static const int m[3] = { V_BORDERLESS, V_FULLSCREEN, V_WINDOW }; return Tr(m[g_av.mode]); }
        case 2:
            if (g_av.hz == 0) {
                int mx = g_advRates.empty() ? 0 : g_advRates.back();
                if (mx) sprintf_s(buf, n, "%s (%d Hz)", Tr(V_MAX), mx); else strcpy_s(buf, n, Tr(V_MAX));
            } else sprintf_s(buf, n, "%d Hz", g_av.hz);
            return buf;
        case 3:
            if (g_av.maxFps == 0) return Tr(V_UNLIMITED);
            sprintf_s(buf, n, "%d", g_av.maxFps);
            return buf;
        case 4: return g_av.vsync ? Tr(V_YES) : Tr(V_NO);
        case 6: return S.hudArea > 0 ? Tr(V_YES) : Tr(V_NO);
        case 7: return kLangName[g_lang][g_av.lang];
        }
    } else if (g_advSection == 2) {
        if (row == 0) return T.enabled ? Tr(V_YES) : Tr(V_NO);
        if (row == 4) return T.grass ? Tr(V_YES) : Tr(V_NO);
    } else {
        if (row == 0) return S.smaa ? Tr(V_YES) : Tr(V_NO);
        if (row == 8) return S.wpn ? Tr(V_YES) : Tr(V_NO);
    }
    return "";
}

static void RowClick(int row)
{
    if (g_advSection == 0) {
        switch (row) {
        case 1: g_av.mode = (g_av.mode + 1) % 3; break;
        case 2: {
            // 0 (highest) -> each available rate -> back to 0
            size_t i = 0;
            while (i < g_advRates.size() && g_advRates[i] != g_av.hz) i++;
            if (g_av.hz == 0) g_av.hz = g_advRates.empty() ? 0 : g_advRates[0];
            else g_av.hz = i + 1 < g_advRates.size() ? g_advRates[i + 1] : 0;
            break;
        }
        case 3: {
            int k = 0;
            for (int i = 0; i < 6; i++) if (kFpsChoices[i] == g_av.maxFps) k = i;
            g_av.maxFps = kFpsChoices[(k + 1) % 6];
            break;
        }
        case 4: g_av.vsync = !g_av.vsync; break;
        case 6: S.hudArea = S.hudArea > 0 ? 0 : 1.778f; break;
        case 7: g_av.lang = (g_av.lang + 1) % LANG_COUNT; break;
        }
    } else if (g_advSection == 2) {
        if (row == 0) T.enabled = !T.enabled;
        if (row == 4) T.grass = !T.grass;
    } else {
        if (row == 0) S.smaa = !S.smaa;
        if (row == 8) S.wpn = !S.wpn;
    }
}

// ---------------------------------------------------------------- load / apply / revert

static wchar_t* GamePath(const wchar_t* name)
{
    static wchar_t p[2][MAX_PATH];
    static int k;
    k ^= 1;
    swprintf_s(p[k], L"%s%s", g_dir, name);
    return p[k];
}

static int GameIniInt(const wchar_t* sec, const wchar_t* key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, GamePath(L"GtEngine.ini"));
}

static float GameIniFloat(const wchar_t* sec, const wchar_t* key, float def)
{
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, GamePath(L"GtEngine.ini"));
    return (float)_wtof(buf);
}

static float CoefFromFov(float fov) { return tanf(fov * 3.14159265f / 360.0f) * 1.875f; }
static float FovFromCoef(float c) { return atanf(c / 1.875f) * 360.0f / 3.14159265f; }

static float ScanFor(float coef)
{
    // terrain culling wedge: covers the screen corners (+10 degrees), at least for 21:9
    float aspect = fmaxf((float)g_w / (float)g_h, 2.39f);
    float x = 0.3f * coef * aspect, y = 0.3f * coef;
    float a = atanf(sqrtf(x * x + y * y)) * 180.0f / 3.14159265f + 10;
    return fminf(85, fmaxf(45, ceilf(a)));
}

static bool g_fovPending;     // the player object keeps its own copy: refresh it in game

static void ApplyFovLive()
{
    g_fovPending = true;
    float c = CoefFromFov(g_av.fov);
    if (float* p = CfgF(CFG_FOVCOEF)) *p = c;
    if (float* p = CfgF(CFG_SCANANGLE)) *p = ScanFor(c);
}

static void LoadRates()
{
    g_advRates.clear();
    int w = GameIniInt(L"GRAPHICS", L"BackBufferWidth", (int)g_w), h = GameIniInt(L"GRAPHICS", L"BackBufferHeight", (int)g_h);
    DEVMODEW dm = { };
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++) {
        if ((int)dm.dmPelsWidth != w || (int)dm.dmPelsHeight != h || dm.dmDisplayFrequency < 50) continue;
        int hz = (int)dm.dmDisplayFrequency;
        if (std::find(g_advRates.begin(), g_advRates.end(), hz) == g_advRates.end()) g_advRates.push_back(hz);
    }
    std::sort(g_advRates.begin(), g_advRates.end());
}

static void AdvLoad()
{
    float coef = GameIniFloat(L"GRAPHICS", L"FOVCoef", 2.4882f);
    if (float* p = CfgF(CFG_FOVCOEF)) coef = *p;
    g_av.fov = floorf(FovFromCoef(coef) + 0.5f);
    bool full = GameIniInt(L"GRAPHICS", L"FullScreen", 0) != 0;
    g_av.mode = full ? 1 : (IniF(L"General", L"Borderless", 1) != 0 ? 0 : 2);
    g_av.hz = (int)IniF(L"Display", L"RefreshRate", 0);
    int mf = GameIniInt(L"GRAPHICS", L"MaxFPS", 100000);
    g_av.maxFps = (GameIniInt(L"GRAPHICS", L"LimitFPS", 1) == 0 || mf >= 1000) ? 0 : mf;
    g_av.vsync = GameIniInt(L"GRAPHICS", L"VerticalRetraceSync", 1) != 0;
    g_av.lang = g_lang;
    LoadRates();
    g_avSaved = g_av;
    g_advSavedS = S;
    g_advSavedT = T;
    g_advSavedCoef = CfgF(CFG_FOVCOEF) ? *CfgF(CFG_FOVCOEF) : coef;
    g_advSavedScan = CfgF(CFG_SCANANGLE) ? *CfgF(CFG_SCANANGLE) : 73;
}

static void AdvRevert()
{
    g_av = g_avSaved;
    // only the values this page edits go back; everything else in S may have changed meanwhile (F10)
    const Settings& o = g_advSavedS;
    S.smaa = o.smaa; S.sharpen = o.sharpen; S.sharpenStrength = o.sharpenStrength;
    S.bloom = o.bloom; S.bloomIntensity = o.bloomIntensity; S.ao = o.ao; S.aoIntensity = o.aoIntensity;
    S.fog = o.fog; S.fogMax = o.fogMax; S.rays = o.rays; S.raysIntensity = o.raysIntensity;
    S.dof = o.dof; S.dofStrength = o.dofStrength; S.shadows = o.shadows; S.shStrength = o.shStrength;
    S.wpn = o.wpn; S.flash = o.flash; S.flashStrength = o.flashStrength; S.color = o.color; S.vibrance = o.vibrance;
    S.hudScale = o.hudScale; S.hudArea = o.hudArea;
    T = g_advSavedT;
    if (float* p = CfgF(CFG_FOVCOEF)) *p = g_advSavedCoef;
    if (float* p = CfgF(CFG_SCANANGLE)) *p = g_advSavedScan;
    g_fovPending = true;
}

static void WriteFx(const wchar_t* sec, const wchar_t* key, float v)
{
    wchar_t b[32];
    swprintf_s(b, L"%g", v);
    WritePrivateProfileStringW(sec, key, b, GamePath(L"DeadhuntFX.ini"));
}

static void WriteGame(const wchar_t* key, const wchar_t* fmt, double v)
{
    wchar_t b[32];
    swprintf_s(b, fmt, v);
    WritePrivateProfileStringW(L"GRAPHICS", key, b, GamePath(L"GtEngine.ini"));
}

static void BackupOnce(const wchar_t* name)
{
    wchar_t dst[MAX_PATH];
    swprintf_s(dst, L"%s%s.antes_avanzado", g_dir, name);
    CopyFileW(GamePath(name), dst, TRUE);   // only the first time
}

static bool g_advAskRestart;     // APLICAR saved settings that only take effect after a restart

// Starts a new copy of the game and ends this one. The new process waits in DllMain until this one
// has exited (DHFX_WAIT_PID), so they never run at the same time.
static void RestartGame()
{
    wchar_t exe[MAX_PATH], pid[16];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    swprintf_s(pid, L"%lu", GetCurrentProcessId());
    static wchar_t cmd[2048];
    wcsncpy_s(cmd, GetCommandLineW(), _TRUNCATE);
    SetEnvironmentVariableW(L"DHFX_WAIT_PID", pid);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(exe, cmd, nullptr, nullptr, FALSE, 0, nullptr, g_dir, &si, &pi);
    SetEnvironmentVariableW(L"DHFX_WAIT_PID", nullptr);
    if (!ok) { Log("Reinicio: no se pudo lanzar el juego (%lu)", GetLastError()); return; }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log("Reinicio: nuevo proceso %lu, cerrando este", pi.dwProcessId);
    ExitProcess(0);
}

static void AdvApply()
{
    g_advAskRestart = g_av.mode != g_avSaved.mode || g_av.hz != g_avSaved.hz || g_av.maxFps != g_avSaved.maxFps ||
                      g_av.vsync != g_avSaved.vsync || g_av.lang != g_avSaved.lang;
    BackupOnce(L"GtEngine.ini");
    BackupOnce(L"DeadhuntFX.ini");
    float coef = CoefFromFov(g_av.fov);
    WriteGame(L"FOVCoef", L"%.4f", coef);
    WriteGame(L"ScanAngle", L"%.0f", ScanFor(coef));
    WriteGame(L"FullScreen", L"%.0f", g_av.mode == 1 ? 1 : 0);
    WriteGame(L"VerticalRetraceSync", L"%.0f", g_av.vsync ? 1 : 0);
    WriteGame(L"LimitFPS", L"%.0f", 1);
    WriteGame(L"MaxFPS", L"%.0f", g_av.maxFps ? g_av.maxFps : 100000);
    // the game writes FullScreen back from memory when it saves its own options
    if (int* p = CfgI(CFG_FULLSCREEN)) *p = g_av.mode == 1 ? 1 : 0;
    ApplyFovLive();
    WriteFx(L"General", L"Borderless", g_av.mode == 2 ? 0.0f : 1.0f);
    WriteFx(L"Display", L"RefreshRate", (float)g_av.hz);
    WritePrivateProfileStringW(L"General", L"Language", kLangCode[g_av.lang], GamePath(L"DeadhuntFX.ini"));
    WriteFx(L"SMAA", L"Enabled", S.smaa);
    WriteFx(L"Sharpen", L"Enabled", S.sharpen);     WriteFx(L"Sharpen", L"Strength", S.sharpenStrength);
    WriteFx(L"Bloom", L"Enabled", S.bloom);         WriteFx(L"Bloom", L"Intensity", S.bloomIntensity);
    WriteFx(L"AO", L"Enabled", S.ao);               WriteFx(L"AO", L"Intensity", S.aoIntensity);
    WriteFx(L"Fog", L"Enabled", S.fog);             WriteFx(L"Fog", L"MaxOpacity", S.fogMax);
    WriteFx(L"GodRays", L"Enabled", S.rays);        WriteFx(L"GodRays", L"Intensity", S.raysIntensity);
    WriteFx(L"DOF", L"Enabled", S.dof);             WriteFx(L"DOF", L"Strength", S.dofStrength);
    WriteFx(L"Shadows", L"Enabled", S.shadows);     WriteFx(L"Shadows", L"Strength", S.shStrength);
    WriteFx(L"WeaponLight", L"Enabled", S.wpn);
    WriteFx(L"MuzzleFlash", L"Enabled", S.flash);   WriteFx(L"MuzzleFlash", L"Strength", S.flashStrength);
    WriteFx(L"Color", L"Enabled", S.color);         WriteFx(L"Color", L"Vibrance", S.vibrance);
    WriteFx(L"HUD", L"Scale", S.hudScale);          WriteFx(L"HUD", L"Area", S.hudArea);
    WriteFx(L"Terrain", L"Enabled", T.enabled);     WriteFx(L"Terrain", L"Parallax", T.parallax);
    WriteFx(L"Terrain", L"Relief", T.relief);       WriteFx(L"Terrain", L"AntiTile", T.antiTile);
    WriteFx(L"Terrain", L"Grass", T.grass);
    WriteFx(L"Terrain", L"Objects", T.objects);     WriteFx(L"Terrain", L"ObjectRelief", T.objRelief);
    g_avSaved = g_av;
    g_advSavedS = S;
    g_advSavedT = T;
    g_advSavedCoef = coef;
    g_advSavedScan = ScanFor(coef);
    g_advApplied = true;
    Log("AVANZADO: ajustes guardados (FOV %.0f, modo %d, %d Hz, FPS %d, vsync %d)", g_av.fov, g_av.mode, g_av.hz,
        g_av.maxFps, (int)g_av.vsync);
}

static void AdvDefaults()
{
    if (g_advSection == 0) {
        g_av.fov = 106; ApplyFovLive();
        g_av.mode = 0; g_av.hz = 0; g_av.maxFps = 0; g_av.vsync = true;
        S.hudScale = 1; S.hudArea = 0;
    } else if (g_advSection == 2) {
        T.enabled = true; T.parallax = 9; T.relief = 0.6f; T.antiTile = 1; T.grass = true;
        T.objects = true; T.objRelief = 0.8f;
    } else {
        S.smaa = true;
        S.sharpen = true; S.sharpenStrength = 0.6f;
        S.bloom = true;   S.bloomIntensity = 0.35f;
        S.ao = true;      S.aoIntensity = 1.0f;
        S.fog = true;     S.fogMax = 0.55f;
        S.rays = true;    S.raysIntensity = 0.6f;
        S.dof = false;    S.dofStrength = 0.7f;
        S.shadows = true; S.shStrength = 0.5f;
        S.wpn = true;
        S.flash = true;   S.flashStrength = 1.5f;
        S.color = true;   S.vibrance = 0.2f;
    }
}

static void AdvOpen()
{
    AdvLoad();
    g_advAskRestart = false;
    g_advOn = true;
    g_advApplied = false;
    g_advDrag = -1;
}

static void AdvClose(bool revert)
{
    if (!g_advOn) return;
    if (revert) AdvRevert();
    g_advOn = false;
    g_advDrag = -1;
}

// ---------------------------------------------------------------- texts (inside the game's text pass)

// set by menu_skin.inl: draw a GUI text / panel / slider in the new menu style
static bool (*g_skinTextFn)(int, float, float, float, float, DWORD, const char*, DWORD);
static void (*g_skinPanelFn)(IDirect3DDevice9*, const float*, bool);
static void (*g_skinSliderFn)(IDirect3DDevice9*, const float*, float);
static bool (*g_skinOnFn)();

static void EngText(int size, float x1, float y1, float x2, float y2, DWORD col, const char* s, DWORD flags)
{
    if (!s || !*s) return;
    if (g_skinOnFn && g_skinOnFn() && g_skinTextFn(size, x1, y1, x2, y2, col, s, flags)) return;
    if (!g_tabFont) return;
    ((PFN_EngText)(void*)EngTextOrig)(g_tabFont, nullptr, size, x1, y1, x2, y2, col, s, flags);
}

static DWORD HoverCol(const float* r, DWORD normal)
{
    return InRect(r, g_cursorX, g_cursorY) ? g_colHover : normal;
}

static void TabRectAdv(float r[4])
{
    float sp = g_tabCtrl[1] - g_tabSnd[1];
    r[0] = g_tabCtrl[0]; r[1] = g_tabCtrl[1] + sp; r[2] = g_tabCtrl[2]; r[3] = g_tabCtrl[3] + sp;
}

// Our tab label, plus the whole page when AVANZADO is open. Called while the game draws "CONTROLES".
static void AdvDrawTexts(int page)
{
    g_advHitN = 0;
    for (auto& s : g_advSliders) s.used = false;
    int sz = g_tabSize;
    float tab[4];
    TabRectAdv(tab);
    // hit rects a bit larger than the text line (mouse comfort)
    float th = tab[3] - tab[1];
    AddHit(tab[0], tab[1], tab[0] + (tab[2] - tab[0]) * 1.4f, tab[1] + th, HIT_TAB);
    DWORD tc = g_advOn ? g_colTabSel : HoverCol(g_advHits[g_advHitN - 1].r, g_colTab);
    EngText(sz, tab[0], tab[1], tab[2] + UX(60), tab[3], tc, Tr(TAB_ADV), TXT_LEFT);
    if (!g_advOn || page != PAGE_CTRL) return;
    AddHit(g_tabCtrl[0], g_tabCtrl[1], g_tabCtrl[0] + (g_tabCtrl[2] - g_tabCtrl[0]) * 1.4f, g_tabCtrl[1] + th, HIT_CTRLTAB);

    float lh = (float)sz;                                    // line height
    // section switches
    const char* secName[3] = { Tr(SEC_SCREEN), Tr(SEC_FX), Tr(SEC_TERRAIN) };
    for (int i = 0; i < 3; i++) {
        float r[4] = { UX(200.0f + i * 160), VY(40), UX(200.0f + i * 160 + 150), VY(40) + lh };
        AddHit(r[0], r[1], r[2], r[3], HIT_SEC0 + i);
        DWORD c = g_advSection == i ? COL_VALUE : HoverCol(r, g_colTab);
        EngText(sz, r[0], r[1], r[2], r[3], c, secName[i], TXT_CENTER);
    }
    // rows
    int n = RowCount();
    float top = VY(84), step = VY(g_advSection == 1 ? 38.5f : g_advSection == 0 ? 45.0f : 50.0f);
    char buf[64];
    for (int i = 0; i < n; i++) {
        const AdvRow& r = RowDef(i);
        float y = top + i * step;
        EngText(sz, UX(150), y, UX(415), y + lh, COL_LABEL, Tr(r.label), TXT_RIGHT);
        const char* val = RowValueText(i, buf, sizeof(buf));
        if (r.kind == RK_SLIDER) {
            float sr[4] = { UX(438), y + VY(1), UX(660), y + VY(1) + g_h * 0.027f };
            g_advSliders[i] = { { sr[0], sr[1], sr[2], sr[3] }, SliderGet(i), true };
            AddHit(sr[0] - UX(4), y, sr[2] + UX(4), y + lh, HIT_SLIDER + i);
            EngText(sz, UX(672), y, UX(760), y + lh, COL_VALUE, val, TXT_LEFT);
        } else {
            float vr[4] = { UX(438), y, UX(438) + UX(240), y + lh };
            AddHit(vr[0], vr[1], vr[2], vr[3], HIT_ROW + i);
            EngText(sz, vr[0], vr[1], vr[2], vr[3], HoverCol(vr, COL_VALUE), val, TXT_LEFT);
        }
    }
    if (g_advAskRestart) {                                   // after APLICAR: offer the restart
        int s2 = (int)(sz * 0.8f);
        float y = VY(500);
        EngText(s2, UX(150), y, UX(560), y + lh, COL_VALUE, Tr(ASK_RESTART), TXT_LEFT);
        float rr[4] = { UX(565), y, UX(665), y + lh }, lr[4] = { UX(670), y, UX(770), y + lh };
        AddHit(rr[0], rr[1], rr[2], rr[3], HIT_RESTART);
        AddHit(lr[0], lr[1], lr[2], lr[3], HIT_LATER);
        EngText(s2, rr[0], rr[1], rr[2], rr[3], HoverCol(rr, COL_VALUE), Tr(B_RESTART), TXT_CENTER);
        EngText(s2, lr[0], lr[1], lr[2], lr[3], HoverCol(lr, g_colTab), Tr(B_LATER), TXT_CENTER);
    } else if (g_advSection == 0)
        EngText((int)(sz * 0.8f), UX(190), VY(470), UX(700), VY(470) + lh, COL_DIM,
                Tr(NOTE_RESTART), TXT_LEFT);
    // bottom buttons (where the controls page has PREDETERMINADO, and APLICAR like the graphics page)
    float by = VY(538);
    float dr[4] = { UX(430), by, UX(680), by + lh };
    AddHit(dr[0], dr[1], dr[2], dr[3], HIT_DEFAULTS);
    EngText(sz, dr[0], dr[1], dr[2], dr[3], HoverCol(dr, g_colTab), Tr(B_DEFAULTS), TXT_CENTER);
    float ar[4] = { UX(660), by, UX(728), by + lh };
    AddHit(ar[0] - UX(10), ar[1], ar[2], ar[3], HIT_APPLY);
    EngText(sz, ar[0] - UX(60), ar[1], ar[2], ar[3], HoverCol(ar, g_colTab), Tr(B_APPLY), TXT_RIGHT);
}

// Called for every engine text draw. Returns true to drop the text.
static bool AdvOnText(void* font, DWORD* a)
{
    if (!S.advEnabled) return false;
    const char* s = (const char*)a[6];
    if (!s || IsBadReadPtr(s, 1)) return false;
    int page = OptionsPage();
    if (page == PAGE_NONE) return false;
    float* r = (float*)&a[1];
    if (strcmp(s, Tr(G_SOUND)) == 0) {
        memcpy(g_tabSnd, r, sizeof(g_tabSnd));
        return false;
    }
    if (strcmp(s, Tr(G_CONTROLS)) == 0) {
        memcpy(g_tabCtrl, r, sizeof(g_tabCtrl));
        g_tabFont = font;
        g_tabSize = (int)a[0];
        g_advTabsSeen = true;
        if (page == PAGE_CTRL && !g_advOn) g_colTabSel = a[5];
        if (g_advOn && page == PAGE_CTRL) {                    // our tab is the selected one now
            float hr[4] = { r[0], r[1], r[2] + (r[2] - r[0]) * 0.4f, r[3] };
            a[5] = HoverCol(hr, g_colTab);
        }
        if (g_tabSnd[1] > 0 && g_tabSnd[1] < r[1]) AdvDrawTexts(page);
        return false;
    }
    // with AVANZADO open, everything of the controls page right of the tab column goes
    if (g_advOn && page == PAGE_CTRL && r[0] > g_w * 0.17f) return true;
    return false;
}

// ---------------------------------------------------------------- geometry (frame notch, sliders)

static int g_advSliderStage;      // 0 nothing yet, 1 bar replayed (fill next), 2 done
static float g_advBar[4];

static bool DrawBBox(IDirect3DDevice9* d, INT base, UINT minIdx, UINT numV, float bb[4])
{
    IDirect3DVertexBuffer9* vb = nullptr;
    UINT off = 0, stride = 0;
    d->GetStreamSource(0, &vb, &off, &stride);
    if (!vb || !stride || base < 0) { if (vb) vb->Release(); return false; }
    void* p = nullptr;
    bool ok = SUCCEEDED(vb->Lock(off + ((UINT)base + minIdx) * stride, numV * stride, &p, D3DLOCK_READONLY)) && p;
    if (ok) { HudBBox((const unsigned char*)p, numV, stride, false, bb); vb->Unlock(); }
    vb->Release();
    return ok;
}

// Redraws the CONTROLES frame with the vertices of its tab notch moved one tab down.
static bool DrawShiftedFrame(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt, INT base, UINT minIdx, UINT numV,
                             UINT startIdx, UINT prims)
{
    float sp = g_tabCtrl[1] - g_tabSnd[1];
    if (sp <= 1) return false;
    float lo = g_tabCtrl[1] - sp * 0.5f, hi = g_tabCtrl[1] + sp * 1.4f;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    UINT off = 0, stride = 0;
    d->GetStreamSource(0, &vb, &off, &stride);
    d->GetIndices(&ib);
    bool done = false;
    if (vb && ib && stride >= 12 && base >= 0) {
        D3DINDEXBUFFER_DESC idesc;
        ib->GetDesc(&idesc);
        UINT isz = idesc.Format == D3DFMT_INDEX32 ? 4 : 2;
        UINT icount = pt == D3DPT_TRIANGLELIST ? prims * 3 : prims + 2;
        std::vector<unsigned char> verts(numV * stride);
        std::vector<unsigned char> idx(icount * isz);
        void *pv = nullptr, *pi = nullptr;
        if (SUCCEEDED(vb->Lock(off + ((UINT)base + minIdx) * stride, numV * stride, &pv, D3DLOCK_READONLY)) && pv) {
            memcpy(verts.data(), pv, verts.size());
            vb->Unlock();
            if (SUCCEEDED(ib->Lock(startIdx * isz, icount * isz, &pi, D3DLOCK_READONLY)) && pi) {
                memcpy(idx.data(), pi, idx.size());
                ib->Unlock();
                // indices are relative to 'base'; make them relative to minIdx for the UP call
                for (UINT i = 0; i < icount; i++) {
                    if (isz == 2) ((WORD*)idx.data())[i] -= (WORD)minIdx;
                    else ((DWORD*)idx.data())[i] -= minIdx;
                }
                // screen pixels per vertex unit: find the mesh axis that moves along the screen's y
                float p0[3] = { 0, 0, 0 }, s0[2];
                HudTransform(p0, s0);
                int axis = -1;
                float k = 0;
                for (int ax = 0; ax < 3; ax++) {
                    float p1[3] = { 0, 0, 0 }, s1[2];
                    p1[ax] = 1;
                    HudTransform(p1, s1);
                    if (fabsf(s1[1] - s0[1]) > fabsf(k)) { k = s1[1] - s0[1]; axis = ax; }
                }
                if (axis >= 0 && fabsf(k) > 1e-6f) {
                    for (UINT i = 0; i < numV; i++) {
                        float* p = (float*)(verts.data() + (size_t)i * stride);
                        float sc[2];
                        HudTransform(p, sc);
                        if (sc[1] > lo && sc[1] < hi) p[axis] += sp / k;
                    }
                    oDIPUP(d, pt, 0, numV, prims, idx.data(), isz == 4 ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                           verts.data(), stride);
                    done = true;
                }
            }
        }
        d->SetStreamSource(0, vb, off, stride);
        d->SetIndices(ib);
    }
    if (vb) vb->Release();
    if (ib) ib->Release();
    return done;
}

static void DrawRemapped(IDirect3DDevice9* d, const HudXf& t, D3DPRIMITIVETYPE pt, INT b, UINT mn, UINT nv, UINT si, UINT n)
{
    D3DMATRIX np = ProjFor(t, g_w, g_h), op = g_curProjM;
    oSetTransform(d, D3DTS_PROJECTION, &np);
    oDIP(d, pt, b, mn, nv, si, n);
    oSetTransform(d, D3DTS_PROJECTION, &op);
}

// DIP hook: returns true when the draw was handled (replaced or hidden).
static bool AdvDraw(IDirect3DDevice9* d, D3DPRIMITIVETYPE pt, INT b, UINT mn, UINT nv, UINT si, UINT n)
{
    if (!g_advOn || g_inFX || !g_advTabsSeen || Is2D(d) || OptionsPage() != PAGE_CTRL) return false;
    if (fabsf(g_curProjM._34) > 0.5f) return false;
    float bb[4];
    if (!DrawBBox(d, b, mn, nv, bb)) return false;
    float w = bb[2] - bb[0], h = bb[3] - bb[1];
    if (w >= g_w * 0.98f && h >= g_h * 0.98f) return false;                 // background picture
    if (bb[0] < g_w * 0.06f && w > g_w * 0.8f && n > 40) {                   // the tabbed frame
        if (g_skinOnFn && g_skinOnFn()) { g_skinPanelFn(d, bb, false); return true; }
        return DrawShiftedFrame(d, pt, b, mn, nv, si, n);
    }
    if (bb[0] < g_w * 0.17f) return false;
    // controls page content: hidden, except the first slider whose artwork draws ours
    if (h < g_h * 0.05f && g_advSliderStage < 2) {
        if (g_advSliderStage == 0 && w > g_w * 0.15f && g_skinOnFn && g_skinOnFn()) {
            for (auto& s : g_advSliders) if (s.used) g_skinSliderFn(d, s.r, s.v);
            g_advSliderStage = 2;
        } else if (g_advSliderStage == 0 && w > g_w * 0.15f) {
            memcpy(g_advBar, bb, sizeof(bb));
            for (auto& s : g_advSliders) {
                if (!s.used) continue;
                HudXf t;
                t.ax = (s.r[2] - s.r[0]) / w;  t.bx = s.r[0] - bb[0] * t.ax;
                t.ay = (s.r[3] - s.r[1]) / h;  t.by = s.r[1] - bb[1] * t.ay;
                DrawRemapped(d, t, pt, b, mn, nv, si, n);
            }
            g_advSliderStage = 1;
        } else if (g_advSliderStage == 1 && bb[0] >= g_advBar[0] - 2 && w > 1) {
            float bw = g_advBar[2] - g_advBar[0], bh = g_advBar[3] - g_advBar[1];
            float pad = bb[0] - g_advBar[0];
            for (auto& s : g_advSliders) {
                if (!s.used || s.v < 0.005f) continue;
                float ax = (s.r[2] - s.r[0]) / bw, ay = (s.r[3] - s.r[1]) / bh;
                float left = s.r[0] + pad * ax;
                float width = s.v * ((s.r[2] - s.r[0]) - 2 * pad * ax);
                HudXf t;
                t.ax = width / w;  t.bx = left - bb[0] * t.ax;
                t.ay = ay;         t.by = s.r[1] - g_advBar[1] * ay;
                DrawRemapped(d, t, pt, b, mn, nv, si, n);
            }
            g_advSliderStage = 2;
        }
    }
    return true;
}

// ---------------------------------------------------------------- mouse (DirectInput)

static bool g_advMouseDown, g_advCaptured;

// Returns true when the game must not see this button state (the click is ours).
static bool AdvMouseButton(bool down)
{
    bool edge = down != g_advMouseDown;
    g_advMouseDown = down;
    if (!S.advEnabled) return false;
    if (!edge) return g_advCaptured;
    int page = OptionsPage();
    if (down) {
        g_advCaptured = false;
        if (page == PAGE_NONE || !g_advTabsSeen) return false;
        int hit = HitAt(g_cursorX, g_cursorY);
        if (hit == HIT_TAB) {
            if (!g_advOn) {
                AdvOpen();
                if (page != PAGE_CTRL) { g_advSwitchNow = true; g_advSwitchWait = 90; }
            }
            g_advCaptured = true;
        } else if (g_advOn && page == PAGE_CTRL) {
            bool content = g_cursorX > g_tabCtrl[2] + (g_tabCtrl[2] - g_tabCtrl[0]) * 0.5f;
            if (hit == HIT_CTRLTAB) AdvClose(true);
            else if (hit >= HIT_SEC0 && hit <= HIT_SEC2) g_advSection = hit - HIT_SEC0;
            else if (hit == HIT_APPLY) AdvApply();
            else if (hit == HIT_DEFAULTS) AdvDefaults();
            else if (hit == HIT_RESTART) RestartGame();
            else if (hit == HIT_LATER) g_advAskRestart = false;
            else if (hit >= HIT_SLIDER) { g_advDrag = hit - HIT_SLIDER; }
            else if (hit >= HIT_ROW) RowClick(hit - HIT_ROW);
            g_advCaptured = hit != 0 || content;
        }
        return g_advCaptured;
    }
    bool was = g_advCaptured;
    g_advCaptured = false;
    g_advDrag = -1;
    return was;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_DICreateDevice)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIGetState)(IDirectInputDevice8A*, DWORD, LPVOID);
typedef HRESULT(STDMETHODCALLTYPE* PFN_DIGetData)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
typedef HRESULT(WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static PFN_DICreateDevice oDICreateDevice;
static PFN_DIGetState oDIGetState;
static PFN_DIGetData oDIGetData;
static PFN_DI8Create oDI8Create;
static IDirectInputDevice8A* g_mouseDev, *g_kbdDev;
static const GUID kGuidSysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID kGuidSysKeyboard = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static HRESULT STDMETHODCALLTYPE hkDIGetState(IDirectInputDevice8A* dev, DWORD cb, LPVOID data)
{
    HRESULT hr = oDIGetState(dev, cb, data);
    if (dev == g_mouseDev && SUCCEEDED(hr) && data && cb >= sizeof(DIMOUSESTATE)) {
        BYTE* btn = ((DIMOUSESTATE*)data)->rgbButtons;
        GamepadMouseState((DIMOUSESTATE*)data, cb);
        if (AdvMouseButton((btn[0] & 0x80) != 0)) btn[0] = 0;
    }
    if (dev == g_kbdDev && SUCCEEDED(hr) && data && cb >= 256) GamepadKeyboardState((BYTE*)data);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDIGetData(IDirectInputDevice8A* dev, DWORD cbObj, LPDIDEVICEOBJECTDATA od,
                                             LPDWORD inOut, DWORD flags)
{
    DWORD cap = inOut ? *inOut : 0;
    HRESULT hr = oDIGetData(dev, cbObj, od, inOut, flags);
    bool real = SUCCEEDED(hr) && od && inOut && cbObj >= sizeof(DIDEVICEOBJECTDATA) && !(flags & DIGDD_PEEK);
    if (real && dev == g_mouseDev) GamepadMouseData(od, cbObj, cap, inOut);
    if (real && dev == g_kbdDev) GamepadKeyboardData(od, cbObj, cap, inOut);
    if (dev == g_mouseDev && SUCCEEDED(hr) && od && inOut && cbObj >= sizeof(DIDEVICEOBJECTDATA) && !(flags & DIGDD_PEEK)) {
        DWORD k = 0;
        for (DWORD i = 0; i < *inOut; i++) {
            DIDEVICEOBJECTDATA* e = (DIDEVICEOBJECTDATA*)((BYTE*)od + (size_t)i * cbObj);
            bool drop = e->dwOfs == DIMOFS_BUTTON0 && AdvMouseButton((e->dwData & 0x80) != 0);
            if (!drop) {
                if (k != i) memcpy((BYTE*)od + (size_t)k * cbObj, e, cbObj);
                k++;
            }
        }
        *inOut = k;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkDICreateDevice(IDirectInput8A* di, REFGUID g, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN u)
{
    HRESULT hr = oDICreateDevice(di, g, out, u);
    bool mouse = IsEqualGUID(g, kGuidSysMouse), kbd = IsEqualGUID(g, kGuidSysKeyboard);
    if (SUCCEEDED(hr) && out && *out && (mouse || kbd)) {
        if (mouse) g_mouseDev = *out; else g_kbdDev = *out;
        // both devices share one vtable: hook it once, tell them apart by pointer
        if (void* o = HookVtable(*out, 9, (void*)hkDIGetState)) oDIGetState = (PFN_DIGetState)o;
        if (void* o = HookVtable(*out, 10, (void*)hkDIGetData)) oDIGetData = (PFN_DIGetData)o;
        Log(mouse ? "Raton DirectInput enganchado (AVANZADO, mando)" : "Teclado DirectInput enganchado (mando)");
    }
    return hr;
}

static HRESULT WINAPI hkDI8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN u)
{
    HRESULT hr = oDI8Create(inst, ver, riid, out, u);
    if (SUCCEEDED(hr) && out && *out)
        if (void* o = HookVtable(*out, 3, (void*)hkDICreateDevice)) oDICreateDevice = (PFN_DICreateDevice)o;
    return hr;
}

static void HookDirectInput()
{
    HMODULE di = LoadLibraryW(L"dinput8.dll");
    void* target = di ? (void*)GetProcAddress(di, "DirectInput8Create") : nullptr;
    if (!target) return;
    unsigned char* base = (unsigned char*)GetModuleHandleW(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++)
        for (void** f = (void**)(base + d->FirstThunk); *f; f++)
            if (*f == target) {
                DWORD old;
                VirtualProtect(f, 4, PAGE_READWRITE, &old);
                oDI8Create = (PFN_DI8Create)*f;
                *f = (void*)hkDI8Create;
                VirtualProtect(f, 4, old, &old);
            }
}

// ---------------------------------------------------------------- per frame

// In game: copy the configured FOV to the player camera (+0x1a78, rebuilt by 0x480f90) and the
// terrain culling wedge (+0x34 of [0x6b71e8]), like the engine's own FOV debug keys do.
static void ApplyFovToPlayer()
{
    if (!g_fovPending || !g_frameHad3D) return;
    g_fovPending = false;
    float* coef = CfgF(CFG_FOVCOEF);
    float* scan = CfgF(CFG_SCANANGLE);
    DWORD pl = *(DWORD*)0x6b71e0, sb = *(DWORD*)0x6b71e8;
    if (coef && pl && !IsBadWritePtr((void*)(pl + 0x1a78), 4) && *(float*)(pl + 0x1a78) != *coef) {
        *(float*)(pl + 0x1a78) = *coef;
        ((void(__fastcall*)(DWORD, void*))0x480f90)(pl, nullptr);
    }
    if (scan && sb && !IsBadWritePtr((void*)(sb + 0x34), 4)) *(float*)(sb + 0x34) = *scan;
}

static void AdvFrame()
{
    ApplyFovToPlayer();
    if (!S.advEnabled) return;
    int page = OptionsPage();
    if (g_advSwitchNow) {
        g_advSwitchNow = false;
        ((void(__cdecl*)())0x53d900)();                     // the CONTROLES tab button's handler
    }
    if (g_advOn) {
        if (g_advSwitchWait > 0) {
            g_advSwitchWait--;
            if (page == PAGE_CTRL) g_advSwitchWait = 0;
        } else if (page != PAGE_CTRL) {
            AdvClose(true);                                  // left through another tab or ATRAS
        }
        if (g_advDrag >= 0 && g_advDrag < 16 && g_advSliders[g_advDrag].used) {
            const float* r = g_advSliders[g_advDrag].r;
            SliderSet(g_advDrag, (g_cursorX - r[0]) / (r[2] - r[0]));
        }
    }
    if (page == PAGE_NONE) g_advTabsSeen = false;
    g_advSliderStage = 0;
}
