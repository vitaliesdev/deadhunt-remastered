// DeadhuntFX - wider mouse sensitivity slider (CONTROLES tab).
//
// The game maps the slider linearly: sensitivity = 0.001 + position * 0.049 (0x53c3fa), and back
// when the tab opens (0x53c330). Both sites are replaced by calls to a curve in two exponential
// halves: the left half goes from [Input] MinSensitivity (default 0.0002) up to 0.001, the old
// minimum, which is now the middle; the right half keeps the old range up to 0.05.
// Included from main.cpp before DllMain.

static float g_sensMin = 0.0002f;
static const float kSensMid = 0.001f, kSensMax = 0.05f;

static float __cdecl SensFromPos(float pos)
{
    pos = fminf(fmaxf(pos, 0.0f), 1.0f);
    if (pos < 0.5f) return g_sensMin * powf(kSensMid / g_sensMin, pos * 2);
    return kSensMid * powf(kSensMax / kSensMid, pos * 2 - 1);
}

static float __cdecl PosFromSens(float s)
{
    if (s <= g_sensMin) return 0;
    if (s < kSensMid) return logf(s / g_sensMin) / logf(kSensMid / g_sensMin) * 0.5f;
    return fminf(0.5f + logf(s / kSensMid) / logf(kSensMax / kSensMid) * 0.5f, 1.0f);
}

// st0 = SensFromPos([ecx+0x44]) (slider position)
static __declspec(naked) void SensStub()
{
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [ecx + 0x44]
        call SensFromPos
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// st0 = PosFromSens([eax+8]) (current sensitivity)
static __declspec(naked) void SensPosStub()
{
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [eax + 8]
        call PosFromSens
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// Menu cursor speed (slider +0x34, value [input+4]): the game maps 10 + pos * 170 (0x53c41b, back at
// 0x53c353). Same treatment: the old minimum 10 is the middle, [Input] MinMenuSensitivity (2) the left end.
static float g_menuMin = 2.0f;
static const float kMenuMid = 10.0f, kMenuMax = 180.0f;

static float __cdecl MenuSensFromPos(float pos)
{
    pos = fminf(fmaxf(pos, 0.0f), 1.0f);
    if (pos < 0.5f) return g_menuMin * powf(kMenuMid / g_menuMin, pos * 2);
    return kMenuMid * powf(kMenuMax / kMenuMid, pos * 2 - 1);
}

static float __cdecl PosFromMenuSens(float s)
{
    if (s <= g_menuMin) return 0;
    if (s < kMenuMid) return logf(s / g_menuMin) / logf(kMenuMid / g_menuMin) * 0.5f;
    return fminf(0.5f + logf(s / kMenuMid) / logf(kMenuMax / kMenuMid) * 0.5f, 1.0f);
}

// st0 = MenuSensFromPos([edx+0x44])
static __declspec(naked) void MenuSensStub()
{
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [edx + 0x44]
        call MenuSensFromPos
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

// st0 = PosFromMenuSens([ecx+4])
static __declspec(naked) void MenuPosStub()
{
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [ecx + 4]
        call PosFromMenuSens
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

static bool PatchCall15(DWORD addr, const unsigned char* orig, void* fn)
{
    unsigned char* p = (unsigned char*)addr;
    if (IsBadReadPtr(p, 15) || memcmp(p, orig, 15)) return false;
    DWORD old;
    VirtualProtect(p, 15, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE8;
    *(DWORD*)(p + 1) = (DWORD)fn - (DWORD)(p + 5);
    memset(p + 5, 0x90, 10);
    VirtualProtect(p, 15, old, &old);
    return true;
}

// All GUI sliders (0x543f90, the "traveller" constructor) convert the mouse x to a position with
// the vertical scale (height / 600) instead of the horizontal one (width / 800). On 4:3 both match;
// on 16:9 the click lands far from the thumb and it jumps to the end. Use width / 800.
static float g_guiMinus800 = -800.0f;

static void FixSliderScale()
{
    struct { DWORD addr; unsigned char orig[6]; } sites[4] = {
        { 0x544057, { 0xd8, 0x35, 0x0c, 0x7b, 0x67, 0x00 } },     // fdiv [-600]
        { 0x544063, { 0x8b, 0x82, 0xd4, 0x07, 0x00, 0x00 } },     // mov eax, [edx+0x7d4] (height)
        { 0x544084, { 0xd8, 0x35, 0x0c, 0x7b, 0x67, 0x00 } },
        { 0x54408f, { 0x8b, 0x88, 0xd4, 0x07, 0x00, 0x00 } },     // mov ecx, [eax+0x7d4]
    };
    for (auto& s : sites)
        if (IsBadReadPtr((void*)s.addr, 6) || memcmp((void*)s.addr, s.orig, 6)) { Log("Barras del menu: no se pudo corregir la escala"); return; }
    DWORD old;
    VirtualProtect((void*)0x544050, 0x50, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)(0x544057 + 2) = (DWORD)&g_guiMinus800;
    *(DWORD*)(0x544084 + 2) = (DWORD)&g_guiMinus800;
    *(unsigned char*)(0x544063 + 2) = 0xd0;                        // [+0x7d0] = width
    *(unsigned char*)(0x54408f + 2) = 0xd0;
    VirtualProtect((void*)0x544050, 0x50, old, &old);
    Log("Barras del menu: escala horizontal corregida (ancho/800)");
}

static void HookMouseSlider()
{
    FixSliderScale();
    g_sensMin = IniF(L"Input", L"MinSensitivity", 0.0002f);
    if (g_sensMin <= 0 || g_sensMin >= kSensMid) g_sensMin = 0.0002f;
    static const unsigned char toSens[15] = { 0xd9, 0x41, 0x44, 0xd8, 0x0d, 0xd4, 0x83, 0x67, 0x00, 0xd8, 0x05, 0x1c, 0x78, 0x67, 0x00 };
    static const unsigned char toPos[15]  = { 0xd9, 0x40, 0x08, 0xd8, 0x25, 0x1c, 0x78, 0x67, 0x00, 0xd8, 0x35, 0xd4, 0x83, 0x67, 0x00 };
    bool ok = !IsBadReadPtr((void*)0x53c330, 0xe0) && !memcmp((void*)0x53c3fa, toSens, 15) &&
              !memcmp((void*)0x53c330, toPos, 15);      // both or none
    if (ok) {
        PatchCall15(0x53c3fa, toSens, (void*)SensStub);
        PatchCall15(0x53c330, toPos, (void*)SensPosStub);
    }
    Log("Raton: barra de sensibilidad ampliada %s (minimo %.5f, centro %.4f, maximo %.3f)",
        ok ? "si" : "NO", g_sensMin, kSensMid, kSensMax);
    g_menuMin = IniF(L"Input", L"MinMenuSensitivity", 2.0f);
    if (g_menuMin <= 0 || g_menuMin >= kMenuMid) g_menuMin = 2.0f;
    static const unsigned char mToSens[15] = { 0xd9, 0x42, 0x44, 0xd8, 0x0d, 0xd0, 0x83, 0x67, 0x00, 0xd8, 0x05, 0xa0, 0x76, 0x67, 0x00 };
    static const unsigned char mToPos[15]  = { 0xd9, 0x41, 0x04, 0xd8, 0x25, 0xa0, 0x76, 0x67, 0x00, 0xd8, 0x35, 0xd0, 0x83, 0x67, 0x00 };
    bool mok = !memcmp((void*)0x53c41b, mToSens, 15) && !memcmp((void*)0x53c353, mToPos, 15);
    if (mok) {
        PatchCall15(0x53c41b, mToSens, (void*)MenuSensStub);
        PatchCall15(0x53c353, mToPos, (void*)MenuPosStub);
    }
    Log("Raton: barra del menu ampliada %s (minimo %.1f, centro %.0f, maximo %.0f)", mok ? "si" : "NO", g_menuMin, kMenuMid, kMenuMax);
}

// The CONTROLES page only copies its two sliders into the input settings when the page is closed
// (controls window vtable slot 16, 0x53c3e0), so moving them showed no change. While the page is open
// the values are applied live, both with our curves.
static void MouseLiveFrame()
{
    if (OptionsPage() != PAGE_CTRL) return;
    DWORD m = *(DWORD*)ENG_GUI;
    DWORD win = *(DWORD*)(m + 0x124);
    DWORD io = *(DWORD*)0x6b71f0;
    if (!win || !io || IsBadReadPtr((void*)win, 0x38) || IsBadReadPtr((void*)io, 0x18)) return;
    DWORD inp = *(DWORD*)(io + 0x14);
    DWORD tGame = *(DWORD*)(win + 0x30), tMenu = *(DWORD*)(win + 0x34);
    if (!inp || IsBadWritePtr((void*)inp, 12) || !tGame || !tMenu ||
        IsBadReadPtr((void*)(tGame + 0x44), 4) || IsBadReadPtr((void*)(tMenu + 0x44), 4)) return;
    float pg = *(float*)(tGame + 0x44), pm = *(float*)(tMenu + 0x44);
    if (pg >= 0 && pg <= 1) *(float*)(inp + 8) = SensFromPos(pg);
    if (pm >= 0 && pm <= 1) *(float*)(inp + 4) = MenuSensFromPos(pm);
}
