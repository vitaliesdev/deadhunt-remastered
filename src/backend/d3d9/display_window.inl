// Display mode: refresh rate, borderless window, Reset and CreateDevice hooks. (part of the unity build, see src/main.cpp)
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

