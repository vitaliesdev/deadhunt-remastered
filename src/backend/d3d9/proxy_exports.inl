// d3d9.dll proxy: loads the real d3d9 (DXVK or system), mode list, Direct3DCreate9. (part of the unity build, see src/main.cpp)
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

