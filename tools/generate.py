r"""Generates every file that is derived from the game's own data (or from our sources) into build\:
    build\DeadhuntFX\hud_atlas.bin         HUD glyphs and icons          (hud\)
    build\DeadhuntFX\lang\*.csv            game texts per language       (Data\Data.pk5 + tools\lang)
    build\Data\FontE, build\Data\FontP     fonts with accents / Polish   (Data\Fonts)
    L10n.inl                               the mod's own texts           (tools\l10n.py)
The game files are read from your own copy of Deadhunt and must be the untouched Steam originals
(their SHA-256 is checked), so the repository does not need to carry the game's fonts or texts.

    python tools\generate.py "<Deadhunt folder>"
"""
import hashlib, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BUILD = os.path.join(REPO, "build")

# SHA-256 of the original Steam files
ORIGINAL = {
    r"Data\Data.pk5": "782621a64a377a83753bbedae9288db6f195d7367a406bc2aff000b3f63de2a5",
    r"Fonts\EngineFont1.dat": "50e67115b76c5de771265354301eca25f78f2c277693f7304ac762387efdf138",
    r"Fonts\EngineFont1.jpg": "398d1ef617f642149a3a10e59e8e18d293f4dab7f169f06dd23ad9581d75a701",
    r"Fonts\EngineFont2.dat": "50e67115b76c5de771265354301eca25f78f2c277693f7304ac762387efdf138",
    r"Fonts\EngineFont2.jpg": "b0efbe01b1da5c24b245337323552b39c2fa4026a659ddd29dbba67cfb3dfadb",
    r"Fonts\_A_EngineFont1.jpg": "4dca5ce880f6272b355fac35ca4c4cf4aa818a16b49ab03d1c783888ee1598ad",
    r"Fonts\_A_EngineFont2.jpg": "1b90a694944d4d7ceea2e290a8939764a5c1d46906212112439fcb1f136dc193",
}


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def find_original(game, candidates, key):
    """First candidate whose hash is the original one."""
    for c in candidates:
        p = os.path.join(game, c)
        if os.path.isfile(p) and sha256(p) == ORIGINAL[key]:
            return p
    sys.exit("ERROR: no original copy of %s found in %s (checked %s).\n"
             "Verify the game files in Steam (Properties > Installed Files > Verify) and try again."
             % (key, game, ", ".join(candidates)))


def run(*args):
    print(">", " ".join(os.path.basename(a) if i == 1 else a for i, a in enumerate(args)))
    subprocess.check_call([sys.executable] + list(args))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    game = sys.argv[1]
    if not os.path.isfile(os.path.join(game, "Deadhunt.exe")):
        sys.exit("ERROR: Deadhunt.exe not found in " + game)

    # an untouched backup (Data.pk5BK, Data\Fonts_original) is used if the live files were modded
    pk5 = find_original(game, [r"Data\Data.pk5", r"Data\Data.pk5BK"], r"Data\Data.pk5")
    fonts = None
    for d in (r"Data\Fonts", r"Data\Fonts_original"):
        names = [k.split("\\", 1)[1] for k in ORIGINAL if k.startswith("Fonts")]
        if all(os.path.isfile(os.path.join(game, d, n)) and
               sha256(os.path.join(game, d, n)) == ORIGINAL["Fonts\\" + n] for n in names):
            fonts = os.path.join(game, d)
            break
    if not fonts:
        sys.exit("ERROR: no original Data\\Fonts found in " + game + ". Verify the game files in Steam.")
    print("game data:", pk5, "|", fonts)

    out = os.path.join(BUILD, "DeadhuntFX")
    os.makedirs(out, exist_ok=True)
    run(os.path.join(HERE, "hudgen.py"), os.path.join(REPO, "hud"), os.path.join(REPO, "hud", "iconsheet.png"),
        os.path.join(out, "hud_atlas.bin"))
    run(os.path.join(HERE, "font_build.py"), fonts, os.path.join(BUILD, "Data", "FontE"), "cp1252")
    run(os.path.join(HERE, "font_build.py"), fonts, os.path.join(BUILD, "Data", "FontP"), "cp1250")
    run(os.path.join(HERE, "build_texts.py"), pk5, os.path.join(out, "lang"))
    run(os.path.join(HERE, "l10n.py"), os.path.join(REPO, "L10n.inl"))
    print("generate: OK")


if __name__ == "__main__":
    main()
