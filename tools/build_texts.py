r"""Writes DeadhuntFX\lang\GUILocalize_<lang>.csv / Localize_<lang>.csv from the original csv files in
Data\Data.pk5 (the untouched package): every translated (or fixed) line is replaced, the rest keep their
original bytes. Russian starts from the package's own (rus) files, every other language from the English
ones. GameData.inl serves them to the game instead of the package's files.
    python build_texts.py [original Data.pk5] [out dir]
"""
import os, sys, importlib
from pk5 import read_pk5, name_of
from build_es_common import translate

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else r"E:\SteamLibrary\steamapps\common\Deadhunt\Data\Data.pk5BK"
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(HERE), "DeadhuntFX", "lang")
# language: (module with GUI / LOC, encoding); en and ru only carry fixes of the original texts
LANGS = {"es": ("es", "cp1252"), "en": ("lang.en", "ascii"), "ru": ("lang.ru", "cp1251"),
         "fr": ("lang.fr", "cp1252"), "de": ("lang.de", "cp1252"), "it": ("lang.it", "cp1252"),
         "pt": ("lang.pt", "cp1252"), "pl": ("lang.pl", "cp1250"), "uk": ("lang.uk", "cp1251")}

sys.path.insert(0, HERE)
files = {name_of(raw): data for raw, data in read_pk5(SRC)}
os.makedirs(OUT, exist_ok=True)
for code, (mod, enc) in LANGS.items():
    if code == "es":
        G, L = importlib.import_module("es_gui").GUI, importlib.import_module("es_loc").LOC
    else:
        m = importlib.import_module(mod)
        G, L = m.GUI, m.LOC
    tag = "(rus)" if code == "ru" else ""
    for name, table in (("GUILocalize", G), ("Localize", L)):
        raw = files["Data\\Base\\%s%s.csv" % (name, tag)]
        data = translate(raw, table, enc)
        with open(os.path.join(OUT, "%s_%s.csv" % (name, code)), "wb") as f:
            f.write(data)
    print(code, enc, len(G), len(L))
