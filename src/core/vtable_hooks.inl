// COM vtable hooking helpers and key polling. (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- hooks

typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                     D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static PFN_CreateDevice oCreateDevice;
static PFN_Present oPresent;
static PFN_Reset oReset;
static HWND g_hwnd;

static void* HookVtable(void* obj, int index, void* fn)
{
    void** vt = *(void***)obj;
    if (vt[index] == fn) return nullptr;
    DWORD old;
    VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* orig = vt[index];
    vt[index] = fn;
    VirtualProtect(&vt[index], sizeof(void*), old, &old);
    return orig;
}

static bool KeyPressed(int vk, bool& was)
{
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool fg = false;
    if (HWND w = GetForegroundWindow()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        fg = pid == GetCurrentProcessId();
    }
    bool hit = fg && down && !was;
    was = down;
    return hit;
}

