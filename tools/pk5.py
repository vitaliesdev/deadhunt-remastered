import struct, os, sys

GAME = r"E:\SteamLibrary\steamapps\common\Deadhunt"
HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "ex")


def read_pk5(path):
    d = bytes(b ^ 0xFF for b in open(path, "rb").read())
    n = struct.unpack("<I", d[:4])[0]
    entries = []
    p = 4
    for _ in range(n):
        raw = d[p:p + 260]
        size, off = struct.unpack("<II", d[p + 260:p + 268])
        entries.append((raw, d[off:off + size]))
        p += 268
    return entries


def name_of(raw):
    return raw.split(b"\0")[0].decode("latin1")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "extract":
        for raw, data in read_pk5(os.path.join(GAME, "Data", "Data.pk5BK")):
            name = name_of(raw)
            print(len(data), name)
            out = os.path.join(EX, name.replace("\\", os.sep))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, "wb").write(data)
