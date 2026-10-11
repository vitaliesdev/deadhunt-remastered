// DeadhuntFX - Xbox-style controller support through XInput (comes with Windows; Steam Input also
// presents PlayStation and other pads as XInput).
//
// The game only reads the DirectInput keyboard and mouse, so the pad is translated into them: the
// mouse device gets the right stick as movement plus buttons and wheel, the keyboard device gets the
// keys of the default bindings. Both the immediate (GetDeviceState) and the buffered
// (GetDeviceData) reads are fed, the latter with press/release events on each change.
//
//   left stick  move (W A S D)            right stick  look / menu cursor
//   RT  fire (left mouse)                 LT  grenade (F)
//   A   jump (space) / menus: click       B   crouch (left ctrl) / menus: back (esc)
//   X   reload (R)                        Y   pick up (E)
//   LB / RB  previous / next weapon (wheel)   d-pad  arrows
//   left stick click  run (left shift)    START  menu (esc)     BACK  tab
// A hit makes the pad rumble.
// [Gamepad] in DeadhuntFX.ini: Enabled, LookSpeed, InvertY, Deadzone, Vibration.
// Included from DeadhuntFX.cpp before AdvancedPage.inl (which owns the DirectInput hooks).

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <xinput.h>

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
static PFN_XInputGetState pXInputGetState;
static PFN_XInputSetState pXInputSetState;

static struct {
    bool enabled, invertY, vibration;
    float lookSpeed, deadzone;
} GP;

static struct {
    bool connected;
    int pad;                         // XInput user index
    XINPUT_STATE st;
    double lastPoll, lastMouseRead;
    float accX, accY;                // fractional mouse counts
    BYTE keys[256];                  // synthetic key state (0x80 = down)
    BYTE keysSent[256];              // as last reported through buffered reads
    bool btn[3], btnSent[3];         // mouse buttons
    int wheel;                       // pending wheel steps
    DWORD rumbleUntil;
} G;

static void GamepadLoadSettings()
{
    GP.enabled   = IniF(L"Gamepad", L"Enabled", 1) != 0;
    GP.lookSpeed = IniF(L"Gamepad", L"LookSpeed", 1.0f);
    GP.invertY   = IniF(L"Gamepad", L"InvertY", 0) != 0;
    GP.deadzone  = IniF(L"Gamepad", L"Deadzone", 0.24f);
    GP.vibration = IniF(L"Gamepad", L"Vibration", 1) != 0;
}

static double GpNowMs()
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static float Stick(SHORT v, float dz)
{
    float x = v / 32767.0f;
    float a = fabsf(x);
    if (a < dz) return 0;
    a = fminf(1, (a - dz) / (1 - dz));
    return x < 0 ? -a : a;
}

// Reads the pad (at most every 4 ms) and rebuilds the synthetic key and button state.
static void GamepadPoll()
{
    if (!GP.enabled) return;
    double now = GpNowMs();
    if (now - G.lastPoll < 4) return;
    G.lastPoll = now;
    if (!pXInputGetState) {
        static bool tried;
        if (tried) return;
        tried = true;
        HMODULE m = LoadLibraryW(L"xinput1_4.dll");
        if (!m) m = LoadLibraryW(L"xinput9_1_0.dll");
        if (m) {
            pXInputGetState = (PFN_XInputGetState)GetProcAddress(m, "XInputGetState");
            pXInputSetState = (PFN_XInputSetState)GetProcAddress(m, "XInputSetState");
        }
        if (!pXInputGetState) { Log("Mando: XInput no disponible"); return; }
    }
    bool was = G.connected;
    G.connected = false;
    for (int i = 0; i < 4 && !G.connected; i++) {
        int p = (G.pad + i) & 3;
        if (pXInputGetState(p, &G.st) == ERROR_SUCCESS) { G.connected = true; G.pad = p; }
    }
    if (G.connected != was) Log(G.connected ? "Mando conectado (XInput %d)" : "Mando desconectado", G.pad);
    memset(G.keys, 0, sizeof(G.keys));
    G.btn[0] = G.btn[1] = G.btn[2] = false;
    if (!G.connected) return;

    const XINPUT_GAMEPAD& p = G.st.Gamepad;
    WORD b = p.wButtons;
    bool menu = !g_frameHad3D;                       // menus and loading screens
    float lx = Stick(p.sThumbLX, GP.deadzone), ly = Stick(p.sThumbLY, GP.deadzone);
    if (!menu) {
        if (ly > 0.3f) G.keys[0x11] = 0x80;          // W
        if (ly < -0.3f) G.keys[0x1F] = 0x80;         // S
        if (lx < -0.3f) G.keys[0x1E] = 0x80;         // A
        if (lx > 0.3f) G.keys[0x20] = 0x80;          // D
        if (b & XINPUT_GAMEPAD_A) G.keys[0x39] = 0x80;              // jump
        if (b & XINPUT_GAMEPAD_B) G.keys[0x1D] = 0x80;              // crouch
        if (b & XINPUT_GAMEPAD_X) G.keys[0x13] = 0x80;              // reload
        if (b & XINPUT_GAMEPAD_Y) G.keys[0x12] = 0x80;              // pick up
        if (b & XINPUT_GAMEPAD_LEFT_THUMB) G.keys[0x2A] = 0x80;     // run
        if (p.bLeftTrigger > 100) G.keys[0x21] = 0x80;              // grenade
        G.btn[0] = p.bRightTrigger > 100;                           // fire
    } else {
        G.btn[0] = (b & XINPUT_GAMEPAD_A) != 0;                     // click
        if (b & XINPUT_GAMEPAD_B) G.keys[0x01] = 0x80;              // back
    }
    if (b & XINPUT_GAMEPAD_START) G.keys[0x01] = 0x80;
    if (b & XINPUT_GAMEPAD_BACK) G.keys[0x0F] = 0x80;
    if (b & XINPUT_GAMEPAD_DPAD_UP) G.keys[0xC8] = 0x80;
    if (b & XINPUT_GAMEPAD_DPAD_DOWN) G.keys[0xD0] = 0x80;
    if (b & XINPUT_GAMEPAD_DPAD_LEFT) G.keys[0xCB] = 0x80;
    if (b & XINPUT_GAMEPAD_DPAD_RIGHT) G.keys[0xCD] = 0x80;
    // weapon cycling: one wheel step per press
    static WORD prev;
    if ((b & XINPUT_GAMEPAD_RIGHT_SHOULDER) && !(prev & XINPUT_GAMEPAD_RIGHT_SHOULDER)) G.wheel += 1;
    if ((b & XINPUT_GAMEPAD_LEFT_SHOULDER) && !(prev & XINPUT_GAMEPAD_LEFT_SHOULDER)) G.wheel -= 1;
    prev = b;
    // rumble
    if (pXInputSetState && GP.vibration) {
        static bool on;
        bool want = GetTickCount() < G.rumbleUntil;
        if (want != on) {
            XINPUT_VIBRATION v = { (WORD)(want ? 42000 : 0), (WORD)(want ? 26000 : 0) };
            pXInputSetState(G.pad, &v);
            on = want;
        }
    }
}

static void GamepadRumble(DWORD ms) { G.rumbleUntil = GetTickCount() + ms; }

// Mouse movement since the previous read: the right stick (and in menus also the left one).
static void GamepadMouseDelta(LONG* dx, LONG* dy)
{
    double now = GpNowMs();
    double dt = G.lastMouseRead > 0 ? fmin(0.1, (now - G.lastMouseRead) / 1000.0) : 0;
    G.lastMouseRead = now;
    if (!G.connected || !GP.enabled) { G.accX = G.accY = 0; return; }
    const XINPUT_GAMEPAD& p = G.st.Gamepad;
    float rx = Stick(p.sThumbRX, GP.deadzone), ry = Stick(p.sThumbRY, GP.deadzone);
    if (!g_frameHad3D) {
        float lx = Stick(p.sThumbLX, GP.deadzone), ly = Stick(p.sThumbLY, GP.deadzone);
        if (fabsf(lx) > fabsf(rx)) rx = lx;
        if (fabsf(ly) > fabsf(ry)) ry = ly;
    }
    // response curve: fine aim near the centre, full speed at the edge
    float cx = rx * fabsf(rx), cy = ry * fabsf(ry);
    float speed = (g_frameHad3D ? 1100.0f : 700.0f) * GP.lookSpeed;      // counts per second at full tilt
    G.accX += (float)(cx * speed * dt);
    G.accY += (float)(-(GP.invertY ? -cy : cy) * speed * dt);
    LONG ix = (LONG)G.accX, iy = (LONG)G.accY;
    G.accX -= ix; G.accY -= iy;
    *dx += ix; *dy += iy;
}

// Immediate reads
static void GamepadMouseState(DIMOUSESTATE* m, DWORD cb)
{
    GamepadPoll();
    GamepadMouseDelta(&m->lX, &m->lY);
    if (!G.connected) return;
    for (int i = 0; i < 3; i++) if (G.btn[i]) m->rgbButtons[i] |= 0x80;
    if (G.wheel) { m->lZ += G.wheel * 120; G.wheel = 0; }
    (void)cb;
}

static void GamepadKeyboardState(BYTE* k)
{
    GamepadPoll();
    if (!G.connected) return;
    for (int i = 0; i < 256; i++) k[i] |= G.keys[i];
}

// Buffered reads: append events for what changed since the last report (if there is room).
static void PushEvent(LPDIDEVICEOBJECTDATA od, DWORD cbObj, DWORD cap, LPDWORD n, DWORD ofs, DWORD data)
{
    if (*n >= cap) return;
    DIDEVICEOBJECTDATA* e = (DIDEVICEOBJECTDATA*)((BYTE*)od + (size_t)(*n) * cbObj);
    memset(e, 0, cbObj);
    e->dwOfs = ofs;
    e->dwData = data;
    e->dwTimeStamp = GetTickCount();
    (*n)++;
}

static void GamepadMouseData(LPDIDEVICEOBJECTDATA od, DWORD cbObj, DWORD cap, LPDWORD n)
{
    GamepadPoll();
    LONG dx = 0, dy = 0;
    GamepadMouseDelta(&dx, &dy);
    if (dx) PushEvent(od, cbObj, cap, n, DIMOFS_X, (DWORD)dx);
    if (dy) PushEvent(od, cbObj, cap, n, DIMOFS_Y, (DWORD)dy);
    if (G.wheel) { PushEvent(od, cbObj, cap, n, DIMOFS_Z, (DWORD)(G.wheel * 120)); G.wheel = 0; }
    for (int i = 0; i < 3; i++)
        if (G.btn[i] != G.btnSent[i]) {
            PushEvent(od, cbObj, cap, n, DIMOFS_BUTTON0 + i, G.btn[i] ? 0x80 : 0);
            G.btnSent[i] = G.btn[i];
        }
}

static void GamepadKeyboardData(LPDIDEVICEOBJECTDATA od, DWORD cbObj, DWORD cap, LPDWORD n)
{
    GamepadPoll();
    for (int i = 0; i < 256; i++)
        if (G.keys[i] != G.keysSent[i] && *n < cap) {
            PushEvent(od, cbObj, cap, n, (DWORD)i, G.keys[i]);
            G.keysSent[i] = G.keys[i];
        }
}
