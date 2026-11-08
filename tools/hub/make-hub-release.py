r"""Prey VR 1.0.1: the 1.0.1 build held since 18 Sep, made for the Game Or Die Installer.

The game (PreyVR.exe, gamex86_64.dll, the libraries) is dist\PreyVR-PCVR-1.0.1's own, byte for
byte: 1.0 plus the message when VR cannot start. What changes is how it installs: Setup.bat on
top, the rest in files\, and a Setup that takes the installer's switches and makes a VR folder
inside the player's Prey. The 1.0 Setup's own steps run as they did, from tools\prepare-data.ps1.

    python tools/hub/make-hub-release.py [the build folder] [output dir] [version, default 1.0.1]
"""
import hashlib, pathlib, re, sys, zipfile

HUB = pathlib.Path(__file__).resolve().parent
REPO = HUB.parent.parent
VERSION = sys.argv[3] if len(sys.argv) > 3 else "1.0.1"
TOP = "Prey-VR-" + VERSION
build = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "dist" / ("PreyVR-PCVR-" + VERSION)
out_dir = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else REPO / "release-hub"

INSTALL = """**The easy way:** download **`Prey VR Installer.exe`** from the
[latest release](https://github.com/GameOrDie007/Prey-2006-VR/releases/latest) and
double-click it. If Windows says it protected your PC, click **More info**, then
**Run anyway**: the installer is not code-signed. Press **INSTALL**. It finds
your Prey, sets everything up and adds a desktop shortcut. Start your headset
streaming, then press **PRESS START**.

**By hand:** unzip the release anywhere and double-click **`Setup.bat`**. It
finds Prey in your Steam libraries or your list of installed programs and makes
a `VR` folder inside it, with PreyVR and a copy of your 13 `.pk4` files (about
1.7 GB). If it cannot find Prey, drag your Prey folder (the one holding
`base`) onto `Setup.bat`. Nothing is downloaded; it runs on the PowerShell that
ships with Windows and needs no administrator. Then start your headset streaming
and run **`Play PreyVR.bat`** in that `VR` folder.

**Updating from 1.0:** run the installer, or the new `Setup.bat`. If you
unzipped 1.0 somewhere, point it at that folder when it asks: it is updated
where it is, and your saves and settings are kept. `Uninstall.bat` in the `VR`
folder removes everything Setup added and keeps your saves; `Collect logs.bat`
puts the logs, crash and freeze reports in one zip on your desktop for a bug
report.

Setup then builds `vr_support.pk4`, the pack PreyVR adds, out of your own Prey
files. Most of what that pack contains is Prey's own - the menu, the HUD, the
weapon models - modified for VR, and those modifications are not the same thing
as the content they modify. So the release carries the modifications and your
install provides the content, which is why nothing here is anybody else's to
give away. If a file in your copy is not the version this was built against,
Setup says so and stops rather than building something subtly wrong; that would
mean a release or a language nobody has tried yet, and it is worth reporting.

"""

files = {}
for p in sorted(build.rglob("*")):
    if not p.is_file(): continue
    rel = p.relative_to(build).as_posix()
    data = p.read_bytes()
    if rel in ("Setup.bat", "Update.bat", "Collect report.bat", "tools/update.ps1"):
        continue	# the 1.0 kit: this Setup, Uninstall.bat and Collect logs.bat replace it
    if rel == "tools/setup.ps1":
        old = b"#   powershell -ExecutionPolicy Bypass -File tools\\setup.ps1 [install dir]"
        assert data.count(old) == 1
        files["files/tools/prepare-data.ps1"] = data.replace(old, b"#   Run by setup.ps1 (Setup.bat): tools\\prepare-data.ps1 <VR folder> -PreyDir <Prey folder>")
    elif rel in ("LICENSE", "COPYING.txt"):
        files[rel] = data
    elif rel == "README.txt":
        t = data.decode("utf-8").replace("\r\n", "\n")
        a = t.index("A release is binaries, a launcher")
        b = t.index("**You need** 64-bit Windows")
        t = t[:a] + INSTALL + t[b:]
        t = t.replace("Config, saves and the log all live inside the folder (`saves\\preybase`), so\nbacking up the folder backs up everything.",
                      "Config, saves and the log all live inside the `VR` folder (`saves\\preybase`),\nso backing it up backs up everything.")
        files[rel] = t.replace("\n", "\r\n").encode("utf-8")
    else:
        files["files/" + rel] = data

files["Setup.bat"] = (HUB / "Setup.bat").read_bytes()
for n in ("setup.ps1", "Uninstall.bat", "Collect logs.bat"):
    files["files/" + n] = (HUB / n).read_bytes()

for rel, data in files.items():
    if rel.endswith((".ps1", ".bat")) and not data.startswith(b"\xef\xbb\xbf"):
        data.decode("ascii")	# PowerShell 5.1 reads a file without a BOM as ANSI: ASCII only
    if rel.lower().endswith(".pk4"):
        raise SystemExit("refusing to ship a pk4 (vr_support.pk4 is built from retail data): " + rel)
for need in ("files/PreyVR.exe", "files/gamex86_64.dll", "files/tools/prepare-data.ps1", "files/tools/buildpk4.ps1",
             "files/tools/pk4patch/manifest.json", "files/Play PreyVR.bat", "files/saves/preybase/preyconfig.cfg", "README.txt"):
    assert need in files, need
v = files["files/version.txt"].decode("ascii")
assert v.split()[:3] == ["PreyVR", "PCVR", VERSION], v

out_dir.mkdir(parents=True, exist_ok=True)
zp = out_dir / (TOP + ".zip")
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as o:
    for rel in sorted(files):
        o.writestr(TOP + "/" + rel, files[rel])
print(zp, zp.stat().st_size, hashlib.sha256(zp.read_bytes()).hexdigest().upper())
print(len(files), "files")
