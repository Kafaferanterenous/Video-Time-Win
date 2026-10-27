"""Create synthetic local fixtures. Usage: python make_test_media.py path/to/ffmpeg.exe"""
from pathlib import Path
import shutil
import subprocess
import sys

target = Path(__file__).resolve().parent / "build" / "synthetic"
target.mkdir(parents=True, exist_ok=True)

def generate(*args):
    subprocess.run([sys.argv[1], "-hide_banner", "-loglevel", "error", *args], check=True)

generate("-f", "lavfi", "-i", "color=c=blue:s=64x48:r=25", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "3.2", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-y", str(target / "video.mp4"))
generate("-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "3.2", "-c:a", "libmp3lame", "-b:a", "128k", "-y", str(target / "audio.mp3"))
generate("-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-t", "3.2", "-c:a", "libmp3lame", "-q:a", "4", "-y", str(target / "vbr.mp3"))
generate("-f", "lavfi", "-i", "color=c=blue:s=64x48", "-frames:v", "1", "-update", "1", "-y", str(target / "photo.jpg"))
shutil.copyfile(target / "photo.jpg", target / "photo.JPEG")
for name in ("bad.mp4", "bad.mp3", "bad.jpg"):
    (target / name).write_bytes(b"broken synthetic test file")
print(f"Synthetic fixtures ready in {target}")
