<div align="center">

<h1>Deadhunt Remastered</h1>

<p><strong>A free fan mod that makes Deadhunt look and run better on modern PCs.</strong></p>

[![Download](https://img.shields.io/badge/Download-Releases-b3261e?style=for-the-badge&logo=github&logoColor=white)](../../releases)
[![Steam](https://img.shields.io/badge/Steam-REPTIX-1b2838?style=for-the-badge&logo=steam&logoColor=white)](https://steamcommunity.com/id/reptilemk/)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-ff5e5b?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/vitalies)
[![MIT license](https://img.shields.io/badge/License-MIT-3d444d?style=for-the-badge)](LICENSE)

[Download](#download) · [Build it yourself](#build-it-yourself) · [Support](#support-the-project)

</div>

## About

A free, unofficial mod for the Steam version of **Deadhunt** (REL Games, 2005). It makes the game look and run better on modern PCs: real-time shadows, ambient occlusion and other effects, an improved terrain, proper widescreen and ultrawide support, a new HUD and menus, partial gamepad support and 9 languages.

It is the same game: no new content and no gameplay changes. It does **not** modify the game's files. Everything is applied in memory by a `d3d9.dll` proxy, and uninstalling leaves the game folder exactly as it was.

## Features

- Real-time shadows, ambient occlusion and other effects
- Improved terrain
- Proper widescreen and ultrawide support
- New HUD and menus
- Partial gamepad support
- 9 languages

## Download

- **Download:** see [Releases](../../releases). Run `DeadhuntRemastered_Setup_v<version>.exe`.
- **Steam guide:** what it does, how to install it and how to set it up.

## Build it yourself

Don't want to trust a prebuilt installer? You can build the exact same installer from this source, using your own copy of the game.

<details>
<summary>Requirements and build steps</summary>

### Requirements
- Windows 10/11
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with *Desktop development with C++* (includes the Windows SDK)
- [Python 3](https://www.python.org/) with `pip install pillow numpy`
- [Inno Setup 6](https://jrsoftware.org/isinfo.php)
- Deadhunt installed from Steam, with **untouched** files (if unsure: Steam > Deadhunt > Properties > Installed Files > Verify integrity)

### Build
```bat
build_release.bat "C:\Program Files (x86)\Steam\steamapps\common\Deadhunt"
```
That single command:
1. `tools\fetch_deps.py` downloads the official **DXVK 3.1.1** and **OpenAL Soft 1.25.2** releases from their GitHub pages and checks their SHA-256 (they are shipped unmodified).
2. `tools\generate.py` reads your game folder (read only), checks that the files are the original ones, and generates into `build\` everything derived from the game's data: the extended fonts, the translated texts and the HUD atlas.
3. `build.bat` compiles `d3d9.dll` and checks the shaders.
4. `tools\stage.py` collects the files into `dist\`.
5. Inno Setup builds `DeadhuntRemastered_Setup_v<version>.exe` (version in the `VERSION` file).

</details>

## Repository layout

| Path | What |
|---|---|
| `DeadhuntFX.cpp`, `*.inl`, `DeadhuntFX.hlsl` | The mod: Direct3D 9 proxy, effects, HUD, menus, terrain, input, languages |
| `ExePatchData.inl` | Small in-memory patches of `Deadhunt.exe` (applied at start-up, never written to disk) |
| `DeadhuntFX.ini`, `dxvk.conf` | Default settings shipped by the installer |
| `tools\` | Build tools: data generators, translations (`tools\lang`, `es_*.py`), installer script |
| `hud\` | HUD icons and fonts (Oxanium, Oswald: SIL Open Font License) |
| `assets\` | Main menu art (upscaled from the original game art) and logo |
| `third_party\` | SMAA and AMD FidelityFX CAS sources, OpenAL Soft licence |

Not in the repository: anything taken from the game's files (fonts, texts) and the upstream binaries. They are generated or downloaded by the build.

## Support the project

This mod is free and made in my spare time. If it saved you time or you simply enjoy it, you can help me keep making tools like this.

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/vitalies)

## Credits

- **Deadhunt Remastered:** REPTIX ([Steam](https://steamcommunity.com/id/reptilemk/))
- **Deadhunt** (c) 2005 REL Games. This is an unofficial fan mod, not affiliated with or endorsed by REL Games or Valve.
- [DXVK](https://github.com/doitsujin/dxvk) (zlib), [OpenAL Soft](https://github.com/kcat/openal-soft) (LGPL), [SMAA](https://github.com/iryoku/smaa) (MIT), [AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS) (MIT), fonts [Oxanium](https://fonts.google.com/specimen/Oxanium) and [Oswald](https://fonts.google.com/specimen/Oswald) (SIL OFL).
- Steam and the Steam logo are trademarks and/or registered trademarks of Valve Corporation.

## Licence

The mod's code and tools are under the [MIT licence](LICENSE). Game content and third-party components keep their own licences (see `LICENSE`).

<div align="center">

[GitHub](https://github.com/vitaliesdev) · [Steam](https://steamcommunity.com/id/reptilemk/) · [Ko-fi](https://ko-fi.com/vitalies)

Made by REPTIX · Vitalie

</div>
