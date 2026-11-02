"""Create a clean copy-ready KNULLI Ports archive, never include user data."""
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile

project = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(sys.argv[1]).resolve()
binary = build / "littlexm-tracker"
header = binary.read_bytes()[:64]
if header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != 183:
    raise SystemExit("Refusing to package: expected a little-endian ARM64 ELF executable")
if not (build / "BUILD-STATUS.txt").is_file():
    raise SystemExit("Missing build verification status")

dist = project / "dist"
dist.mkdir(exist_ok=True)
# A fresh staging directory excludes old userdata even if an earlier package
# was run locally. Replacing the ZIP never deletes the user's unpacked copy.
with tempfile.TemporaryDirectory(prefix="brick-package-", dir=dist) as staging:
    package = pathlib.Path(staging)
    ports = package / "roms" / "ports"
    app = ports / "xm-tracker"  # Retain the installed location and existing user data.
    subprocess.run(["cmake", "--install", str(build), "--prefix", str(app)], check=True)
    packaging = project / "packaging" / "knulli"
    shutil.copy2(packaging / "Little XM Tracker.sh", ports / "Little XM Tracker.sh")
    shutil.copy2(packaging / "gamecontrollerdb.txt", app / "assets" / "gamecontrollerdb.txt")
    shutil.copy2(packaging / "INSTALL.txt", package / "INSTALL.txt")
    shutil.copy2(build / "BUILD-STATUS.txt", app / "BUILD-STATUS.txt")
    # Enforce LF/no BOM for the device shell regardless of checkout line endings.
    launcher = ports / "Little XM Tracker.sh"
    launcher.write_bytes(launcher.read_text(encoding="utf-8-sig").replace("\r\n", "\n").encode("utf-8"))
    for executable in (launcher, app / "littlexm-tracker"):
        executable.chmod(0o755)
    # Empty user folders are safe to merge into an existing installation.
    # Never package their contents or the editable root controller mapping.
    for name in ("songs", "instruments", "samples", "themes", "userdata"):
        directory = app / name
        if directory.is_symlink() or (directory.exists() and
                (not directory.is_dir() or any(directory.iterdir()))):
            raise SystemExit("Refusing to package user content: " + name)
        directory.mkdir(exist_ok=True)
    mapping = app / "gamecontrollerdb.txt"
    if mapping.exists() or mapping.is_symlink():
        raise SystemExit("Refusing to package the editable root controller mapping")
    files = sorted(p for p in package.rglob("*") if p.is_file())
    checksums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(package).as_posix()}\n" for p in files)
    (package / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8", newline="\n")
    archive = dist / "littlexm-tracker-knulli-brick-arm64.zip"
    temporary_archive = archive.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary_archive, "w", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(package))
            elif path.is_dir():
                output.write(path, path.relative_to(package).as_posix() + "/")
    temporary_archive.replace(archive)
    # Do not merge into a previously run unpacked package: that could leak its
    # settings, themes or songs into the next copy-to-device operation. Retire
    # the complete old tree without deleting any files the user put there.
    unpacked = dist / "knulli-brick"
    if unpacked.exists() or unpacked.is_symlink():
        if unpacked.is_symlink() or not unpacked.is_dir() or unpacked.resolve().parent != dist.resolve():
            raise SystemExit("Refusing to move an unexpected copy-ready path: " + str(unpacked))
        previous = dist / "previous-packages"
        if previous.is_symlink() or previous.resolve().parent != dist.resolve():
            raise SystemExit("Refusing to use an unexpected package backup path: " + str(previous))
        previous.mkdir(exist_ok=True)
        backup = previous / ("knulli-brick-" + time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
        if backup.resolve().parent != previous.resolve() or backup.exists():
            raise SystemExit("Refusing to replace a package backup: " + str(backup))
        unpacked.rename(backup)
        print(f"Previous unpacked package preserved: {backup}")
    shutil.copytree(package, unpacked)
    print(f"Brick package: {archive}")
    print(f"Copy-ready folder: {unpacked}")
