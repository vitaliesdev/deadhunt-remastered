r"""Collects the files the Inno Setup installer distributes into dist\ (tools/installer/DeadhuntRemastered.iss).
Only files that do not come with the game; the game's own files are never shipped nor modified.
Everything comes from this repository: the built DLL, build\ (tools/generate.py), assets\ and
third_party\ (tools/fetch_deps.py).
    python tools\stage.py
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BUILD = os.path.join(REPO, "build")
TP = os.path.join(REPO, "third_party")
DIST = os.path.join(REPO, "dist")

if os.path.isdir(DIST):
    shutil.rmtree(DIST)


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


# the mod
put(os.path.join(REPO, "d3d9.dll"), r"game\d3d9.dll")
put(os.path.join(REPO, "DeadhuntFX.hlsl"), r"game\DeadhuntFX.hlsl")
put(os.path.join(REPO, "DeadhuntFX.ini"), r"game\DeadhuntFX.ini")
put(os.path.join(TP, "smaa", "SMAA.hlsl"), r"game\DeadhuntFX\SMAA.hlsl")
put(os.path.join(BUILD, "DeadhuntFX", "hud_atlas.bin"), r"game\DeadhuntFX\hud_atlas.bin")
for f in ("menu_fondo.jpg", "menu_logo.png"):
    put(os.path.join(REPO, "assets", f), "game\\DeadhuntFX\\" + f)
put_dir(os.path.join(BUILD, "DeadhuntFX", "lang"), r"game\DeadhuntFX\lang")
for d in ("FontE", "FontP"):
    put_dir(os.path.join(BUILD, "Data", d), "game\\Data\\" + d)
# licences and credits
put(os.path.join(HERE, "installer", "CREDITOS.txt"), r"game\DeadhuntFX\CREDITOS.txt")
put(os.path.join(TP, "smaa", "LICENSE.txt"), r"game\DeadhuntFX\licencias\SMAA_LICENSE.txt")
put(os.path.join(TP, "cas", "LICENSE.txt"), r"game\DeadhuntFX\licencias\CAS_LICENSE.txt")
put(os.path.join(TP, "openal-soft", "COPYING"), r"game\DeadhuntFX\licencias\OpenAL-Soft_COPYING.txt")
for f in ("OFL-Oxanium.txt", "OFL-Oswald.txt"):
    put(os.path.join(REPO, "hud", f), "game\\DeadhuntFX\\licencias\\" + f)
# optional components (official upstream builds, see tools/fetch_deps.py)
put(os.path.join(TP, "dxvk", "d3d9.dll"), r"dxvk\dxvk_d3d9.dll")
put(os.path.join(REPO, "dxvk.conf"), r"dxvk\dxvk.conf")
put(os.path.join(TP, "openal-soft", "soft_oal.dll"), r"openal\OpenAL32.dll")

total = 0
for r, d, fs in os.walk(DIST):
    for f in fs:
        total += os.path.getsize(os.path.join(r, f))
print("dist: %.2f MB" % (total / 1e6))
