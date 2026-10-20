"""Build independent platform diagnostic archives, never add tools to the patch."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile
from release_info import ROOT, release_info


def sha256(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        return digest.hexdigest()


def package(platform, build, channel, build_date):
    info = release_info(channel, build_date)
    stage = build / f"diagnostics-{platform}"
    if stage.is_symlink():
        raise ValueError("Diagnostic stage must not be a symlink")
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    files = ["diagnose-windows.cmd", "diagnose-windows.ps1"] if platform == "windows" else ["diagnose-linux.sh", "diagnose-linux.py"]
    binaries = ["diagnostic_probe.exe", "executable_check.exe"] if platform == "windows" else ["diagnostic_probe"]
    for name in files:
        shutil.copy2(ROOT / "tools/diagnostics" / name, stage / name)
    for name in binaries:
        shutil.copy2(build / name, stage / name)
    (stage / "README.txt").write_text((ROOT / "docs/diagnostics.txt").read_text().replace("@PATCH_RELEASE@", info["Tag"]), encoding="utf-8")
    license_text = (ROOT / "LICENSE").read_text(encoding="utf-8") + "\nUTFCPP\n" + (ROOT / "vendor/utfcpp/LICENSE").read_text(encoding="utf-8")
    if platform == "windows":
        license_text += "\nPicoSHA2\n" + (ROOT / "vendor/picosha2/LICENSE").read_text(encoding="utf-8")
    (stage / "LICENSE").write_text(license_text, encoding="utf-8")
    metadata = dict(schema_version=1, patch_version=info["Version"], release_tag=info["Tag"],
                    source_commit=info["SourceCommit"], platform=platform,
                    source_tree_dirty=bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True)),
                    patch_sha256=sha256(build / ("eu4_unicode_patch.dll" if platform == "windows" else "libeu4_unicode.so")))
    if platform == "windows":
        metadata["loader_sha256"] = sha256(build / "VERSION.dll")
    metadata["files"] = {path.name: sha256(path) for path in stage.iterdir() if path.is_file()}
    (stage / "diagnostic-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    archive = (ROOT / "dist" / info["WindowsDiagnostics"]) if platform == "windows" else (build / info["LinuxDiagnostics"])
    archive.parent.mkdir(parents=True, exist_ok=True)
    if platform == "windows":
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in sorted(stage.iterdir()):
                output.write(path, path.name)
    else:
        with tarfile.open(archive, "w:gz") as output:
            for path in sorted(stage.iterdir()):
                output.add(path, path.name)
    print(archive)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--build-directory", default="build")
    parser.add_argument("--channel", choices=("Release", "Nightly"), default="Release")
    parser.add_argument("--build-date", default="")
    args = parser.parse_args()
    build = (ROOT / args.build_directory).resolve()
    if not build.is_relative_to(ROOT) or build == ROOT:
        raise ValueError("Build directory must be inside the checkout")
    package(args.platform, build, args.channel, args.build_date)
