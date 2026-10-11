"""Shared pieces of the data build: translation of a localisation csv and the gameplay tweaks."""
import re

# letters our font sets can draw (font_build.py); Ukrainian and Russian use the original cp1251 fonts
SUPPORTED = {"ascii": set(),
             "cp1252": set("áàâäãéèêëíìîïóòôöõúùûüñçÁÀÂÄÃÉÈÊËÍÌÎÏÓÒÔÖÕÚÙÛÜÑÇ¿¡«»"),
             "cp1250": set("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ«»"),
             "cp1251": set("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюяІіЇїЄєҐґ«»№")}
VIEW_MULT = 2
INI_KEYS = [b"FOVCoef", b"ScanAngle", b"LimitFPS", b"MinFPS", b"MaxFPS"]
LINE = re.compile(r'^("(\d+)",\s*")(.*)(",\s*)$')
CRLF = b"\r\n"


def translate(raw, table, enc="cp1252"):
    out, done = [], set()
    for line in raw.split(CRLF):
        m = LINE.match(line.decode("cp1251"))
        if m and m.group(2) in table:
            line = (m.group(1) + table[m.group(2)] + m.group(4)).encode(enc)
            done.add(m.group(2))
        out.append(line)  # untouched lines keep their original bytes
    missing = set(table) - done
    assert not missing, missing
    for k, v in table.items():
        extra = {c for c in v if ord(c) > 127} - SUPPORTED[enc]
        assert not extra, (k, extra)
    return CRLF.join(out)


def tweak_location(data):
    # double ViewRange and ObjectsRange (columns 9 and 10) so distant terrain stays drawn
    lines = data.split(CRLF)
    for i, l in enumerate(lines[1:], 1):
        cells = l.split(b",")
        if len(cells) > 10 and cells[0].strip().strip(b'"').isdigit():
            for c in (9, 10):
                v = int(cells[c].strip().strip(b'"'))
                cells[c] = cells[c].replace(b'"%d"' % v, b'"%d"' % (v * VIEW_MULT))
            lines[i] = b",".join(cells)
    return CRLF.join(lines)


def tweak_profile(data):
    # these rows override GtEngine.ini; drop them so the ini wins
    lines = data.split(CRLF)
    keep = [l for l in lines if not re.match(rb'"GRAPHICS",\s*"(%s)"' % b"|".join(INI_KEYS), l)]
    assert len(lines) - len(keep) == len(INI_KEYS)
    return CRLF.join(keep)
