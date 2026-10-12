// DeadhuntFX - native post-processing for Deadhunt (Direct3D 9).
//
// Built as d3d9.dll in the game folder. Loads the real d3d9 implementation
// (dxvk_d3d9.dll if present, otherwise the system d3d9.dll), hooks
// IDirect3DDevice9::Present/Reset and runs a post-processing chain on the
// back buffer right before each frame is shown.
//
// Files next to the game exe:
//   DeadhuntFX.ini   settings (F10 reloads)
//   DeadhuntFX.hlsl  shaders  (F10 recompiles)
//   DeadhuntFX.log   errors / info

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <string>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <stdio.h>
#include <math.h>
#include <intrin.h>
#include <vector>
#include <algorithm>

// SMAA lookup textures (https://github.com/iryoku/smaa, MIT license)
#include "../third_party/smaa/AreaTex.h"
#include "../third_party/smaa/SearchTex.h"

// ---- modules, in dependency order (unity build: one translation unit)
#include "core/log.inl"
#include "core/settings.inl"
#include "backend/render/shader_compiler.inl"
#include "backend/render/render_targets.inl"
#include "backend/render/post_effects.inl"
#include "core/vtable_hooks.inl"
#include "backend/d3d9/buffer_locks.inl"
#include "backend/d3d9/draw_tracking.inl"

// ---- features (frontend / backend modules)
#include "frontend/l10n/l10n_strings.inl"
#include "backend/game/language.inl"
#include "backend/game/game_data.inl"
#include "backend/render/shadow_map.inl"
#include "backend/render/terrain.inl"
#include "debug/hud_capture.inl"
#include "debug/world_capture.inl"
#include "frontend/hud/hud_layout.inl"
#include "backend/input/gamepad.inl"
#include "frontend/menu/advanced_page.inl"
#include "frontend/hud/weapon_icon.inl"
#include "frontend/hud/new_hud.inl"
#include "frontend/menu/menu_skin.inl"
#include "backend/input/mouse.inl"
#include "frontend/menu/menu_art.inl"

#include "backend/d3d9/draw_hooks.inl"
#include "backend/d3d9/camera_capture.inl"
#include "backend/render/depth_texture.inl"
#include "backend/d3d9/draw_up_and_create_hooks.inl"
#include "backend/render/sun_light.inl"
#include "backend/d3d9/scene_hooks.inl"
#include "backend/d3d9/present.inl"
#include "backend/d3d9/display_window.inl"
#include "backend/d3d9/proxy_exports.inl"
#include "backend/game/engine_errors.inl"
#include "backend/game/text_hook.inl"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // restarted from AVANZADO: wait until the previous copy has fully exited
        wchar_t wpid[16];
        if (GetEnvironmentVariableW(L"DHFX_WAIT_PID", wpid, 16)) {
            SetEnvironmentVariableW(L"DHFX_WAIT_PID", nullptr);
            if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)_wtoi(wpid))) {
                WaitForSingleObject(h, 15000);
                CloseHandle(h);
            }
        }
        GetModuleFileNameW(nullptr, g_dir, MAX_PATH);
        if (wchar_t* s = wcsrchr(g_dir, L'\\')) s[1] = 0;
        LoadSettings();
        TerrainLoadSettings();
        GamepadLoadSettings();
        ApplyExePatch();
        ApplyLanguage();
        HookGameData();
        HookEngineFatal();
        HookExitPaths();
        HookText();
        HookDirectInput();
        HookHudDraw();
        HookMouseSlider();
    }
    return TRUE;
}
