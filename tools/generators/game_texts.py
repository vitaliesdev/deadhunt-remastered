r"""Writes GUILocalize_<lang>.csv / Localize_<lang>.csv from the original csv files in the game's
Data\Data.pk5 (the untouched package) and the translations in localization\game\<lang>.py: every
translated (or fixed) line is replaced, the rest keep their original bytes. Russian starts from the
package's own (rus) files, every other language from the English ones. At run time
src/backend/game/game_data.inl serves them to the game instead of the package's files.
    python tools\generators\game_texts.py <original Data.pk5> <out dir>
"""
import importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "lib"))
from pk5 import read_pk5, name_of
from text_common import translate

LOCALIZATION = os.path.join(REPO, "localization", "game")
# language -> encoding of the game text (en and ru only carry fixes of the original texts)
LANGS = {"es": "cp1252", "en": "ascii", "ru": "cp1251", "fr": "cp1252", "de": "cp1252",
         "it": "cp1252", "pt": "cp1252", "pl": "cp1250", "uk": "cp1251"}


def load(code):
    spec = importlib.util.spec_from_file_location("lang_" + code, os.path.join(LOCALIZATION, code + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.GUI, m.LOC


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    files = {name_of(raw): data for raw, data in read_pk5(src)}
    os.makedirs(out, exist_ok=True)
    for code, enc in LANGS.items():
        gui, loc = load(code)
        tag = "(rus)" if code == "ru" else ""
        for name, table in (("GUILocalize", gui), ("Localize", loc)):
            raw = files["Data\\Base\\%s%s.csv" % (name, tag)]
            with open(os.path.join(out, "%s_%s.csv" % (name, code)), "wb") as f:
                f.write(translate(raw, table, enc))
        print(code, enc, len(gui), len(loc))


if __name__ == "__main__":
    main()
