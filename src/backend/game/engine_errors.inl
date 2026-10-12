// Logging of the engine's fatal errors and unexpected exits. (part of the unity build, see src/main.cpp)
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

