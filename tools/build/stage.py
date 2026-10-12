r"""Collects the files the Inno Setup installer distributes into build\dist\ (installer\DeadhuntRemastered.iss).
Only files that do not come with the game; the game's own files are never shipped nor modified.
Everything comes from this repository: the built DLL (build\bin), the generated data (build\generated,
tools\build\generate.py), config\, shaders\, assets\ and third_party\ (tools\build\fetch_deps.py).
    python tools\build\stage.py
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(REPO, "build", "bin")
GENERATED = os.path.join(REPO, "build", "generated")
DIST = os.path.join(REPO, "build", "dist")
TP = os.path.join(REPO, "third_party")
FONTS = os.path.join(REPO, "assets", "fonts")


def put(src, rel):
    if not os.path.isfile(src):
        sys.exit("ERROR: missing %s (run build_release.bat)" % os.path.relpath(src, REPO))
    dst = os.path.join(DIST, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def put_dir(src, rel):
    if not os.path.isdir(src):
        sys.exit("ERROR: missing %s (run build_release.bat)" % os.path.relpath(src, REPO))
    for f in sorted(os.listdir(src)):
        put(os.path.join(src, f), os.path.join(rel, f))


def main():
    if os.path.isdir(DIST):
        shutil.rmtree(DIST)
    # the mod
    put(os.path.join(BIN, "d3d9.dll"), r"game\d3d9.dll")
    put(os.path.join(REPO, "shaders", "DeadhuntFX.hlsl"), r"game\DeadhuntFX.hlsl")
    put(os.path.join(REPO, "config", "DeadhuntFX.ini"), r"game\DeadhuntFX.ini")
    put(os.path.join(TP, "smaa", "SMAA.hlsl"), r"game\DeadhuntFX\SMAA.hlsl")
    put(os.path.join(GENERATED, "DeadhuntFX", "hud_atlas.bin"), r"game\DeadhuntFX\hud_atlas.bin")
    for f in ("menu_fondo.jpg", "menu_logo.png"):
        put(os.path.join(REPO, "assets", "menu", f), "game\\DeadhuntFX\\" + f)
    put_dir(os.path.join(GENERATED, "DeadhuntFX", "lang"), r"game\DeadhuntFX\lang")
    for d in ("FontE", "FontP"):
        put_dir(os.path.join(GENERATED, "Data", d), "game\\Data\\" + d)
    # licences and credits
    put(os.path.join(REPO, "installer", "CREDITOS.txt"), r"game\DeadhuntFX\CREDITOS.txt")
    put(os.path.join(TP, "smaa", "LICENSE.txt"), r"game\DeadhuntFX\licencias\SMAA_LICENSE.txt")
    put(os.path.join(TP, "cas", "LICENSE.txt"), r"game\DeadhuntFX\licencias\CAS_LICENSE.txt")
    put(os.path.join(TP, "openal-soft", "COPYING"), r"game\DeadhuntFX\licencias\OpenAL-Soft_COPYING.txt")
    for f in ("OFL-Oxanium.txt", "OFL-Oswald.txt"):
        put(os.path.join(FONTS, f), "game\\DeadhuntFX\\licencias\\" + f)
    # optional components (official upstream builds, see tools\build\fetch_deps.py)
    put(os.path.join(TP, "dxvk", "d3d9.dll"), r"dxvk\dxvk_d3d9.dll")
    put(os.path.join(REPO, "config", "dxvk.conf"), r"dxvk\dxvk.conf")
    put(os.path.join(TP, "openal-soft", "soft_oal.dll"), r"openal\OpenAL32.dll")

    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(DIST) for f in fs)
    print("dist: %.2f MB" % (total / 1e6))


if __name__ == "__main__":
    main()
