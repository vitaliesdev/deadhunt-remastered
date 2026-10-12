// Present hook: runs the frame's effects, HUD and per-frame features. (part of the unity build, see src/main.cpp)
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

