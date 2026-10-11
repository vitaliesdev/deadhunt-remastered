// DeadhuntFX - everything the remaster used to change in the game's own files, done in memory instead,
// so the installed game files stay untouched (the installer only adds files):
//
//  - Deadhunt.exe: the code caves and hooks of the old patch_*.py scripts (ExePatchData.inl, made by
//    tools/gen_exepatch.py) are written into the loaded image at start-up; each place gets its usual
//    protection back afterwards. Only the one page that holds the caves stays writable (they keep a
//    frame counter and a few values there), not the whole code section.
//  - Data.pk5: the game loads every file of its package through FileSystem::Load (0x45d7d0, thiscall
//    name, &data, &size; the data goes to one static buffer). That call is wrapped: in Spanish the two
//    localisation files come from DeadhuntFX\lang\*.csv, Location.csv gets twice the draw distance and
//    PrivateProfile.csv loses the rows that would override GtEngine.ini.
// Included from DeadhuntFX.cpp after Language.inl; both run from DllMain, before the game starts.

#include "ExePatchData.inl"

static void ApplyExePatch()
{
    int orig = 0, mod = 0, n = (int)(sizeof(kExeRuns) / sizeof(kExeRuns[0]));
    for (const ExeRun& r : kExeRuns) {
        if (IsBadReadPtr((void*)r.va, r.len)) return;
        if (!memcmp((void*)r.va, r.orig, r.len)) orig++;
        else if (!memcmp((void*)r.va, r.mod, r.len)) mod++;
    }
    DWORD old;
    VirtualProtect((void*)0x676000, 0x1000, PAGE_EXECUTE_READWRITE, &old);   // the cave page (0x676170..0x676270)
    if (mod == n) { Log("Ejecutable: ya parcheado en disco (version de desarrollo)"); return; }
    if (orig != n) { Log("Ejecutable: version desconocida (%d/%d tramos originales); sin parches", orig, n); return; }
    for (const ExeRun& r : kExeRuns) {
        DWORD o;
        VirtualProtect((void*)r.va, r.len, PAGE_EXECUTE_READWRITE, &o);
        memcpy((void*)r.va, r.mod, r.len);
        if ((r.va & ~0xFFFu) != 0x676000) VirtualProtect((void*)r.va, r.len, o, &o);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    Log("Ejecutable: %d parches aplicados en memoria (el archivo no se modifica)", n);
}

// ---------------------------------------------------------------- package files

static std::string g_dataBuf[4];                  // our versions, alive while the game parses them

static bool ReadText(const wchar_t* rel, std::string& out)
{
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s%s", g_dir, rel);
    FILE* f = nullptr;
    if (_wfopen_s(&f, p, L"rb") || !f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(&out[0], 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static std::vector<std::string> SplitStr(const std::string& s, const char* sep)
{
    std::vector<std::string> v;
    size_t a = 0, b, k = strlen(sep);
    while ((b = s.find(sep, a)) != std::string::npos) { v.push_back(s.substr(a, b - a)); a = b + k; }
    v.push_back(s.substr(a));
    return v;
}

static std::string JoinStr(const std::vector<std::string>& v, const char* sep)
{
    std::string r;
    for (size_t i = 0; i < v.size(); i++) { if (i) r += sep; r += v[i]; }
    return r;
}

static std::string StripCell(std::string s)
{
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '"')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"')) s.pop_back();
    return s;
}

static std::string TweakLocation(const std::string& in)          // ViewRange, ObjectsRange (columns 9, 10) x2
{
    std::vector<std::string> lines = SplitStr(in, "\r\n");
    for (size_t i = 1; i < lines.size(); i++) {
        std::vector<std::string> c = SplitStr(lines[i], ",");
        if (c.size() <= 10) continue;
        std::string id = StripCell(c[0]);
        if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) continue;
        for (int k : { 9, 10 }) {
            std::string v = StripCell(c[k]);
            if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) continue;
            std::string from = "\"" + v + "\"", to = "\"" + std::to_string(_atoi64(v.c_str()) * 2) + "\"";
            size_t p = c[k].find(from);
            if (p != std::string::npos) c[k].replace(p, from.size(), to);
        }
        lines[i] = JoinStr(c, ",");
    }
    return JoinStr(lines, "\r\n");
}

static std::string TweakProfile(const std::string& in)           // rows that would override GtEngine.ini
{
    static const char* keys[] = { "FOVCoef", "ScanAngle", "LimitFPS", "MinFPS", "MaxFPS" };
    std::vector<std::string> keep;
    for (auto& l : SplitStr(in, "\r\n")) {
        bool drop = false;
        if (l.compare(0, 11, "\"GRAPHICS\",") == 0) {
            size_t p = 11;
            while (p < l.size() && (l[p] == ' ' || l[p] == '\t')) p++;
            for (const char* k : keys)
                if (l.compare(p, strlen(k) + 2, std::string("\"") + k + "\"") == 0) drop = true;
        }
        if (!drop) keep.push_back(l);
    }
    return JoinStr(keep, "\r\n");
}

typedef int(__fastcall* PFN_FsLoad)(void*, void*, const char*, char**, DWORD*);
static PFN_FsLoad oFsLoad;

static int __fastcall FsLoadHook(void* self, void* edx, const char* name, char** data, DWORD* size)
{
    int r = oFsLoad(self, edx, name, data, size);
    if (!name || !data || !size || IsBadReadPtr(name, 1)) return r;
    // Russian asks for the (rus) names (Language.inl), which stay as the fallback if our file is missing
    int which = !_stricmp(name, "Data\\Base\\GUILocalize.csv") || !_stricmp(name, "Data\\Base\\GUILocalize(rus).csv") ? 0
              : !_stricmp(name, "Data\\Base\\Localize.csv") || !_stricmp(name, "Data\\Base\\Localize(rus).csv") ? 1
              : !_stricmp(name, "Data\\Base\\Location.csv") ? 2 : !_stricmp(name, "Data\\Base\\PrivateProfile.csv") ? 3 : -1;
    if (which < 0) return r;
    std::string& out = g_dataBuf[which];
    bool ok = false;
    if (which <= 1) {
        wchar_t file[64];                                          // every language (en / ru = the originals, fixed)
        swprintf_s(file, L"DeadhuntFX\\lang\\%s_%s.csv", which ? L"Localize" : L"GUILocalize", kLangCode[g_lang]);
        ok = ReadText(file, out);
    } else if (*data && *size && !IsBadReadPtr(*data, *size)) {
        std::string in(*data, *size);
        out = which == 2 ? TweakLocation(in) : TweakProfile(in);
        ok = true;
    }
    if (ok && !out.empty()) {
        *data = &out[0];
        *size = (DWORD)out.size();
        Log("Datos: %s servido por DeadhuntFX (%u bytes)", name, (unsigned)out.size());
    }
    return r;
}

static void HookGameData()
{
    unsigned char* f = (unsigned char*)0x45d7d0;
    static const unsigned char origBytes[6] = { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x30 };
    if (IsBadReadPtr(f, 6) || memcmp(f, origBytes, 6)) { Log("Datos: cargador no reconocido"); return; }
    unsigned char* t = (unsigned char*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return;
    memcpy(t, f, 6);                                                  // trampoline: the 6 moved bytes + jump back
    t[6] = 0xE9;
    *(DWORD*)(t + 7) = (DWORD)(f + 6) - (DWORD)(t + 11);
    oFsLoad = (PFN_FsLoad)t;
    DWORD old;
    VirtualProtect(t, 32, PAGE_EXECUTE_READ, &old);                   // trampoline: executable, not writable
    VirtualProtect(f, 6, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xE9;
    *(DWORD*)(f + 1) = (DWORD)FsLoadHook - (DWORD)(f + 5);
    f[5] = 0x90;
    VirtualProtect(f, 6, old, &old);
}
