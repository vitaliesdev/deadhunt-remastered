// Engine text renderer hook (Advanced tab, HUD text pass). (part of the unity build, see src/main.cpp)
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

