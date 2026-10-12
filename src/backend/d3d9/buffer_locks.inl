// Frame profiler state and vertex/index/texture lock hooks ([Buffers] fix). (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- profiler
// [Debug] Profile=1: logs where the time goes in slow frames.

static bool g_profile, g_inFX;
static double g_qpcMs;

static double NowMs()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * g_qpcMs;
}

struct FrameStats {
    int draws, drawsUP, prims;
    int vbLocks, vbLocksSync, ibLocks, ibLocksSync, texLocks;
    int creates;
    double drawMs, lockMs, texLockMs, createMs;
};
static FrameStats F;

struct VtOrig { void** vt; void* orig; };
static VtOrig g_lockOrig[16];

static void* OrigFor(void* obj)
{
    void** vt = *(void***)obj;
    for (auto& e : g_lockOrig)
        if (e.vt == vt) return e.orig;
    return nullptr;
}

static void HookLock(void* obj, int index, void* fn)
{
    void** vt = *(void***)obj;
    for (auto& e : g_lockOrig)
        if (e.vt == vt) return;
    for (auto& e : g_lockOrig)
        if (!e.vt) {
            e.vt = vt;
            e.orig = HookVtable(obj, index, fn);
            return;
        }
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_BufLock)(void*, UINT, UINT, void**, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* PFN_TexLock)(void*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);

// [Buffers] fix for CPU<->GPU sync stalls on buffer locks (see hkCVB/hkCIB/LockCommon)
static bool g_bufFix;

// Waiting locks grouped by buffer kind, for the profiler log.
struct LockCat { DWORD pool, usage; bool whole; int count; double ms; };
static LockCat g_cats[8];

static void CountCat(DWORD pool, DWORD usage, bool whole, double ms)
{
    for (auto& c : g_cats) {
        if (c.count && c.pool == pool && c.usage == usage && c.whole == whole) { c.count++; c.ms += ms; return; }
    }
    for (auto& c : g_cats) {
        if (!c.count) { c = { pool, usage, whole, 1, ms }; return; }
    }
}

static HRESULT LockCommon(void* self, UINT o, UINT s, void** p, DWORD fl, bool vb)
{
    const DWORD noWait = D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE | D3DLOCK_READONLY;
    bool sync = !(fl & noWait);
    DWORD pool = 0, usage = 0;
    UINT size = 0;
    if (sync && (g_bufFix || g_profile)) {
        if (vb) { D3DVERTEXBUFFER_DESC d; ((IDirect3DVertexBuffer9*)self)->GetDesc(&d); pool = d.Pool; usage = d.Usage; size = d.Size; }
        else    { D3DINDEXBUFFER_DESC d;  ((IDirect3DIndexBuffer9*)self)->GetDesc(&d);  pool = d.Pool; usage = d.Usage; size = d.Size; }
    }
    bool whole = o == 0 && (s == 0 || s >= size);
    // A dynamic buffer rewritten from scratch can be discarded instead of waited on.
    if (sync && g_bufFix && (usage & D3DUSAGE_DYNAMIC) && whole) {
        fl |= D3DLOCK_DISCARD;
        sync = false;
    }
    double t = g_profile ? NowMs() : 0;
    HRESULT hr = ((PFN_BufLock)OrigFor(self))(self, o, s, p, fl);
    if (g_profile && !g_inFX) {
        double ms = NowMs() - t;
        if (vb) { F.vbLocks++; if (sync) F.vbLocksSync++; }
        else    { F.ibLocks++; if (sync) F.ibLocksSync++; }
        F.lockMs += ms;
        if (sync) CountCat(pool, usage, whole, ms);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hkVBLock(void* self, UINT o, UINT s, void** p, DWORD fl)
{
    return LockCommon(self, o, s, p, fl, true);
}

static HRESULT STDMETHODCALLTYPE hkIBLock(void* self, UINT o, UINT s, void** p, DWORD fl)
{
    return LockCommon(self, o, s, p, fl, false);
}

// Static D3DPOOL_DEFAULT buffers are better served from MANAGED under DXVK:
// locks then write to a CPU copy instead of waiting for the GPU.
static void FixBufferPool(DWORD& usage, D3DPOOL& pool)
{
    if (g_bufFix && pool == D3DPOOL_DEFAULT && !(usage & D3DUSAGE_DYNAMIC)) {
        pool = D3DPOOL_MANAGED;
        usage &= D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING | D3DUSAGE_DONOTCLIP |
                 D3DUSAGE_POINTS | D3DUSAGE_NPATCHES;
    }
}

static HRESULT STDMETHODCALLTYPE hkTexLock(void* self, UINT l, D3DLOCKED_RECT* r, const RECT* rc, DWORD fl)
{
    double t = NowMs();
    HRESULT hr = ((PFN_TexLock)OrigFor(self))(self, l, r, rc, fl);
    if (!g_inFX) { F.texLocks++; F.texLockMs += NowMs() - t; }
    return hr;
}

