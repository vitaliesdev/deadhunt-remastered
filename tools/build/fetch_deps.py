r"""Downloads the two optional third-party runtimes from their official GitHub releases and checks
their SHA-256, so the installer only ships the unmodified upstream files:
    third_party\dxvk\d3d9.dll               DXVK 3.1.1, x32\d3d9.dll        (zlib licence)
    third_party\openal-soft\soft_oal.dll    OpenAL Soft 1.25.2, Win32 build  (LGPL)
Files that are already there with the right hash are not downloaded again.

    python tools\build\fetch_deps.py
"""
import hashlib, io, os, sys, tarfile, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
TP = os.path.join(os.path.dirname(os.path.dirname(HERE)), "third_party")

DEPS = [
    {   # DXVK
        "url": "https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-3.1.1.tar.gz",
        "archive_sha256": "40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608",
        "member": "dxvk-3.1.1/x32/d3d9.dll",
        "out": os.path.join(TP, "dxvk", "d3d9.dll"),
        "sha256": "265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa",
    },
    {   # OpenAL Soft
        "url": "https://github.com/kcat/openal-soft/releases/download/1.25.2/openal-soft-1.25.2-bin.zip",
        "archive_sha256": "67a0c4b800bd860c93c04f38caf8cbe4875f9c84700ac430efc451f70e265434",
        "member": "openal-soft-1.25.2-bin/bin/Win32/soft_oal.dll",
        "out": os.path.join(TP, "openal-soft", "soft_oal.dll"),
        "sha256": "606cf8b4c22c7ae00813585279ac045115907bc66d1483eb26355907021ea4b0",
    },
]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


for d in DEPS:
    if os.path.isfile(d["out"]) and sha256(open(d["out"], "rb").read()) == d["sha256"]:
        print("ok  ", os.path.relpath(d["out"], TP))
        continue
    print("get ", d["url"])
    data = urllib.request.urlopen(d["url"]).read()
    if sha256(data) != d["archive_sha256"]:
        sys.exit("ERROR: unexpected SHA-256 for " + d["url"])
    if d["url"].endswith(".zip"):
        member = zipfile.ZipFile(io.BytesIO(data)).read(d["member"])
    else:
        member = tarfile.open(fileobj=io.BytesIO(data)).extractfile(d["member"]).read()
    if sha256(member) != d["sha256"]:
        sys.exit("ERROR: unexpected SHA-256 for " + d["member"])
    os.makedirs(os.path.dirname(d["out"]), exist_ok=True)
    with open(d["out"], "wb") as f:
        f.write(member)
    print("ok  ", os.path.relpath(d["out"], TP))
