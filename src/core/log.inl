// Logging to DeadhuntFX.log. (part of the unity build, see src/main.cpp)
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

