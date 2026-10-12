// DeadhuntFX - game language, [General] Language=es|en|ru|fr|de|it|pt|pl|uk (kLangCode in l10n_strings.inl).
//
// The game reads its texts from Data\Base\GUILocalize.csv and Localize.csv inside Data\Data.pk5 (English;
// the package also holds the Russian ones as *(rus).csv) and its bitmap fonts from Data\Fonts. None of
// that is changed on disk: every language gets its texts from DeadhuntFX\lang (game_data.inl; English and
// Russian are the originals with some fixes, and Russian also gets the (rus) names pushed as the fallback),
// and the Latin languages get their fonts from our sets (tools/generators/fonts.py, which
// reuse some Cyrillic cells): Data\FontE for the cp1252 languages, Data\FontP for Polish. Ukrainian is
// cp1251 like Russian, so it keeps the original fonts.
// Included from main.cpp right after l10n_strings.inl; runs from DllMain, before the game loads data.

static char g_locGui[48], g_locText[48];

static bool PatchPush(DWORD at, DWORD expect, const char* value)
{
    unsigned char* p = (unsigned char*)at;
    if (IsBadReadPtr(p, 5) || p[0] != 0x68 || *(DWORD*)(p + 1) != expect) return false;
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)(p + 1) = (DWORD)value;
    VirtualProtect(p, 5, old, &old);
    return true;
}

// font folder of a language: nullptr = the game's Data\Fonts
static const char* LangFontDir(int lang)
{
    switch (lang) {
    case LANG_EN: case LANG_RU: case LANG_UK: return nullptr;
    case LANG_PL: return "FontP";
    default: return "FontE";
    }
}

static void ApplyLanguage()
{
    wchar_t code[8], ini[MAX_PATH];
    swprintf_s(ini, L"%sDeadhuntFX.ini", g_dir);
    GetPrivateProfileStringW(L"General", L"Language", L"es", code, 8, ini);
    g_lang = LANG_ES;
    for (int i = 0; i < LANG_COUNT; i++)
        if (!_wcsicmp(code, kLangCode[i])) g_lang = i;

    // texts: English = the package's normal files, Russian = its (rus) files, the rest = served by game_data.inl
    bool texts = true;
    if (g_lang == LANG_RU) {
        strcpy_s(g_locGui, "Data\\Base\\GUILocalize(rus).csv");
        strcpy_s(g_locText, "Data\\Base\\Localize(rus).csv");
        texts = PatchPush(0x41fc1a, 0x699038, g_locGui) && PatchPush(0x41fc06, 0x699054, g_locText);
    }

    // fonts: "Data\Fonts\" -> "Data\FontE\" / "Data\FontP\" (same length) wherever the exe names it
    int fontsPatched = 0;
    const char* dir = LangFontDir(g_lang);
    wchar_t path[MAX_PATH];
    if (dir) swprintf_s(path, L"%sData\\%S", g_dir, dir);
    if (dir && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        unsigned char* base = (unsigned char*)GetModuleHandleW(nullptr);
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        static const char from[] = "Data\\Fonts\\";
        char to[sizeof(from)];
        sprintf_s(to, "Data\\%s\\", dir);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
            unsigned char* p = base + sec->VirtualAddress;
            size_t n = sec->Misc.VirtualSize;
            for (size_t k = 0; k + sizeof(from) - 1 <= n; k++)
                if (p[k] == 'D' && !memcmp(p + k, from, sizeof(from) - 1)) {
                    DWORD old;
                    VirtualProtect(p + k, sizeof(from) - 1, PAGE_READWRITE, &old);
                    memcpy(p + k, to, sizeof(from) - 1);
                    VirtualProtect(p + k, sizeof(from) - 1, old, &old);
                    fontsPatched++;
                }
        }
    }
    Log("Idioma: %S (textos DeadhuntFX\\lang%s, fuentes Data\\%s)", kLangCode[g_lang],
        g_lang == LANG_RU && !texts ? ", (rus) NO CAMBIADOS" : "", fontsPatched ? dir : "Fonts");
}
