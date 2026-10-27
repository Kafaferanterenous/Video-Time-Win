"""Package only the executable and public documentation; never include media or settings."""
from pathlib import Path
import hashlib
import shutil
import zipfile

root = Path(__file__).resolve().parent
package = root / "portable_app_videotime"
package.mkdir(exist_ok=True)
source = root / "build" / "portable" / "videotime.exe"
shutil.copy2(source, root / "videotime.exe")
shutil.copy2(source, package / "videotime.exe")
for name in ("README.md", "RELEASE_NOTES.txt", "LICENSE"):
    shutil.copy2(root / name, package / name)
screenshots = ("assets/screenshots/videotime-main.png", "assets/screenshots/videotime-settings.png")
for name in screenshots:
    destination = package / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / name, destination)
names = ("videotime.exe", "README.md", "RELEASE_NOTES.txt", "LICENSE", "PORTABLE_CONTENTS.txt", *screenshots)
(package / "PORTABLE_CONTENTS.txt").write_text(
    "VideoTime 1.2.0 portable package - experimental software\n\nUse at your own risk.\n\n" + "\n".join(names) +
    "\n\nOpen videotime.exe. No install required.\nOptional videotime.ini is created beside the exe when you save preferences.\n",
    encoding="utf-8",
)
archive = root / "Video-Time-Win-v1.2.0-portable.zip"
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
    for name in names:
        output.write(package / name, arcname=name)
with zipfile.ZipFile(archive) as output:
    assert set(output.namelist()) == set(names)
    assert output.testzip() is None
    assert hashlib.sha256(output.read("videotime.exe")).digest() == hashlib.sha256(source.read_bytes()).digest()
    for name in ("README.md", "RELEASE_NOTES.txt"):
        assert b"Use at your own risk." in output.read(name)
hashes = []
for path in (root / "videotime.exe", archive):
    hashes.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}")
(root / "SHA256SUMS-1.2.0.txt").write_text("\n".join(hashes) + "\n", encoding="ascii")
print("Verified portable package: " + archive.name)
print("Contents: " + ", ".join(names))
