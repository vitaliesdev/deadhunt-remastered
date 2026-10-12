# Architecture

Deadhunt Remastered is a `d3d9.dll` proxy. The game loads it instead of the system Direct3D 9; it forwards
everything to the real implementation (DXVK or Windows) and hooks the device to add effects, a new HUD and
menus. It also patches the game in memory (never on disk) and serves its own text files and fonts.

```
build_release.bat ─┬─ tools/build/fetch_deps.py   DXVK + OpenAL Soft (official, hash-checked) → third_party/
                   ├─ tools/build/generate.py     your game copy + assets/ + localization/   → build/generated/
                   ├─ build.bat                   src/ + shaders/ (check)                     → build/bin/d3d9.dll
                   ├─ tools/build/stage.py        everything the installer ships              → build/dist/
                   └─ installer/*.iss             Inno Setup                                  → build/release/*.exe
```

## Source layout (`src/`)

The mod is compiled as **one translation unit**: `src/main.cpp` includes every module (`.inl`) in a fixed
order, and `DllMain` at its end wires them up. Modules share state through file-level `static` variables,
so **a module can only use what is included before it**. Keep the order in `main.cpp` when adding files.

| Layer | Folder | Responsibility |
|---|---|---|
| Core | `src/core/` | Logging (`Log`), settings from `DeadhuntFX.ini` (`Settings S`, `IniF`), vtable hooking helpers. No game knowledge. |
| Backend · Direct3D | `src/backend/d3d9/` | The proxy (`Direct3DCreate9`, loading DXVK/system d3d9), device hooks (draws, scene, Present, Reset), display mode and borderless window. |
| Backend · Render | `src/backend/render/` | Effect chain and its GPU resources, runtime shader compiler, depth texture, sun, shadow maps, terrain/grass/object shaders. |
| Backend · Game | `src/backend/game/` | Integration with `Deadhunt.exe`: in-memory exe patches (`exe_patch_data.inl`, generated), file loader hook serving texts, language and fonts, engine error logging, text renderer hook. |
| Backend · Input | `src/backend/input/` | XInput gamepad mapped to the game's DirectInput, mouse sensitivity range and option sliders. |
| Frontend · HUD | `src/frontend/hud/` | The new HUD (atlas text and icons, damage direction), its layout, weapon icons. |
| Frontend · Menu | `src/frontend/menu/` | Menu skin, menu art, the **Advanced** tab (all mod settings, restart prompt). |
| Frontend · L10n | `src/frontend/l10n/` | `l10n_strings.inl`, generated from `localization/mod/strings.py` (`Tr(id)`). |
| Debug | `src/debug/` | F11 captures (HUD and world) for development. |

Rule of thumb: **backend** talks to the engine and the GPU, **frontend** decides what the player sees and
uses backend services. A few frontend modules still install their own small engine hooks (the HUD draw hook
in `new_hud.inl`, the DirectInput hooks in `advanced_page.inl`); new hooks should go to the backend.

## Data and assets

| Folder | Content | Used by |
|---|---|---|
| `config/` | Default `DeadhuntFX.ini` and `dxvk.conf` shipped by the installer | installer |
| `shaders/` | `DeadhuntFX.hlsl`, compiled at run time from the game folder (F10 reloads) | game |
| `assets/menu/` | Upscaled menu art and logo (loaded with WIC at run time) | game |
| `assets/hud/`, `assets/fonts/` | Icon sheet and fonts the HUD atlas is generated from | `tools/generators/hud_atlas.py` |
| `localization/game/<lang>.py` | Translations of the game's own texts (`GUI` and `LOC` tables by id) | `tools/generators/game_texts.py` |
| `localization/mod/strings.py` | The mod's own texts in every language | `tools/generators/mod_texts.py` |
| `installer/` | Inno Setup script, legal notices (`legal/`), images (`images/`), credits | Inno Setup |

Nothing derived from the game's files is stored in the repository: fonts and texts are generated from the
player's own copy (`tools/build/generate.py` checks it is the original Steam data).

## Adding a feature

1. **Pick the layer.** Engine/GPU work → `src/backend/<area>/`; something the player sees → `src/frontend/<area>/`.
   New area → new folder.
2. **Create the module** (`snake_case.inl`, one short header comment saying what it does) and include it in
   `src/main.cpp` after everything it uses. If it needs start-up work, call it from `DllMain`; if it runs every
   frame, call it from the Present hook (`src/backend/d3d9/present.inl`).
3. **Settings:** add the keys to `config/DeadhuntFX.ini` (with a comment) and read them in
   `src/core/settings.inl` (or a `<Module>LoadSettings()` like the terrain and gamepad).
4. **User-facing options:** add a row to the Advanced tab (`src/frontend/menu/advanced_page.inl`) and its label
   to `localization/mod/strings.py` in all languages.
5. Run `build.bat`, test in game, then `build_release.bat` for an installer.

## Adding a language

1. Add the code and encoding to `CODES`/`ENC` and every string/name in `localization/mod/strings.py`.
2. Add `localization/game/<code>.py` (`GUI` and `LOC` tables) and the code to `LANGS` in
   `tools/generators/game_texts.py`.
3. If its letters are not in the game fonts, extend `tools/generators/fonts.py` and the font folder choice in
   `src/backend/game/language.inl`; the HUD atlas glyphs live in `tools/generators/hud_atlas.py`.
4. Add the installer language, legal notice (`installer/legal/`) and custom messages in
   `installer/DeadhuntRemastered.iss`.

## Releasing

1. Set the version in `VERSION`.
2. `build_release.bat "<Deadhunt folder>"` → `build/release/DeadhuntRemastered_Setup_v<version>.exe`.
3. Commit, tag `v<version>` and attach the installer to a GitHub release.
