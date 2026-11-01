"""Prove the cached MilkyTracker source still matches the pinned archive."""
from pathlib import Path
import hashlib
import sys
import zipfile

root = Path(__file__).resolve().parent.parent
revision = "e3ceb05105e8c5bdc907d6451245bf5a1926b1f9"
archive = root / ".deps" / "milkytracker-source.zip"
expected = "e25a72c6b0a4f656f71ca70c667e03ccb7097ff121889d1df360f51e57245325"
if not archive.is_file():
    sys.exit("Run bash tools/bootstrap.sh first (the pinned archive is required).")
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    sys.exit("FAIL: source archive SHA256 mismatch")
checked = 0
changed = []
with zipfile.ZipFile(archive) as upstream:
    for info in upstream.infolist():
        if info.is_dir() or "/src/milkyplay/" not in info.filename:
            continue
        local = root / ".deps" / info.filename
        if not local.is_file() or local.read_bytes() != upstream.read(info):
            changed.append(info.filename)
        checked += 1
if changed:
    sys.exit("FAIL: modified upstream files:\n" + "\n".join(changed))
print(f"PASS: {checked} MilkyPlay files match upstream {revision}; modifications=0")
