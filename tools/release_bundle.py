"""Assemble and validate a complete, same-commit release from platform artifacts."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import xml.etree.ElementTree as ET
import zipfile
from release_info import ROOT, asset_names, release_info


def digest(data):
    return hashlib.sha256(data).hexdigest()


def archive_files(path):
    result = {}
    def add(name, content):
        name = name.removeprefix("./")
        parts = PurePosixPath(name)
        if not name or parts.is_absolute() or ".." in parts.parts or "\\" in name or name in result:
            raise ValueError(f"Invalid or duplicate archive member: {name}")
        result[name] = content
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError("Archive symlink is not permitted")
                add(entry.filename, archive.read(entry))
    else:
        with tarfile.open(path, "r:gz") as archive:
            for entry in archive:
                if entry.isdir():
                    continue
                if not entry.isfile():
                    raise ValueError("Archive links and special files are not permitted")
                add(entry.name, archive.extractfile(entry).read())
    return result


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def same_identity(metadata, info, platform=False):
    expected = {"source_commit": info["SourceCommit"], "patch_version": info["Version"], "release_tag": info["Tag"]} if platform else {
        "SourceCommit": info["SourceCommit"], "Version": info["Version"], "Tag": info["Tag"], "Channel": info["Channel"]}
    if any(metadata.get(key) != value for key, value in expected.items()) or metadata.get("source_tree_dirty"):
        raise ValueError("Artifact identity differs from the selected clean source commit/channel")


def test_suite(path):
    root = ET.parse(path).getroot()
    tests = list(root.iter("testcase"))
    if int(root.get("tests", str(len(tests)))) != len(tests):
        raise ValueError("Platform test count differs from its report")
    if not tests or any(case.find("failure") is not None or case.find("error") is not None or case.find("skipped") is not None for case in tests):
        raise ValueError("Platform tests are incomplete, failed or skipped")
    if any(int(root.get(key, "0")) != 0 for key in ("failures", "errors", "skipped", "disabled")):
        raise ValueError("Platform test summary contains failures or skips")
    required = {"diagnostic_probe", "unicode_layout", "paragraph_transport", "unicode_search", "legacy_text", "yaml_input"}
    if not required.issubset({case.get("name") for case in tests}):
        raise ValueError("Required platform regression tests are missing")
    return len(tests)


def check_diagnostics(path, platform, info, patch_hash, loader_hash=None):
    files = archive_files(path)
    metadata = json.loads(files["diagnostic-info.json"])
    same_identity(metadata, info, True)
    if metadata.get("platform") != platform or metadata.get("patch_sha256") != patch_hash:
        raise ValueError("Diagnostics do not match the platform patch")
    if loader_hash and metadata.get("loader_sha256") != loader_hash:
        raise ValueError("Windows diagnostic loader identity mismatch")
    inventory = metadata.get("files", {})
    if set(files) != set(inventory) | {"diagnostic-info.json"}:
        raise ValueError("Diagnostic file inventory mismatch")
    if any(digest(files[name]) != hashvalue for name, hashvalue in inventory.items()):
        raise ValueError("Diagnostic payload checksum mismatch")
    required = {"README.txt", "LICENSE"} | ({"diagnose-windows.cmd", "diagnose-windows.ps1", "diagnostic_probe.exe", "executable_check.exe"} if platform == "windows" else {
        "diagnose-linux.sh", "diagnose-linux.py", "diagnostic_probe"})
    if not required.issubset(files):
        raise ValueError("Diagnostic package is incomplete")


def assemble(windows, linux, output, info):
    package = load_json(windows / "build/package-info.json")
    same_identity(package, info)
    validation = load_json(windows / "build/automated-validation.json")
    if validation.get("source_commit") != info["SourceCommit"] or not all(validation.get(key) is True for key in ("passed", "loader_guards", "optional_fonts")):
        raise ValueError("Windows build validation is incomplete")
    windows_tests = test_suite(windows / "build/ctest-results.xml")
    linux_tests = test_suite(linux / "ctest-linux-results.xml")
    linux_info = load_json(linux / "linux-build-info.json")
    same_identity(linux_info, info, True)
    if linux_info.get("ctest_sha256") != digest((linux / "ctest-linux-results.xml").read_bytes()):
        raise ValueError("Linux test report differs from the built package metadata")
    if linux_info.get("excluded_tests"):
        raise ValueError("Excluded tests cannot qualify a release")
    paths = {info[key]: windows / "dist" / info[key] for key in ("PlayerPackage", "FontPackage", "WindowsDiagnostics")}
    paths[info["LinuxPackage"]] = linux / f"EU4UnicodePatch-linux-x86_64-v{info['Version']}.tar.gz"
    paths[info["LinuxDiagnostics"]] = linux / info["LinuxDiagnostics"]
    main = archive_files(paths[info["PlayerPackage"]])
    expected_main = {"VERSION.dll", "plugins/eu4_unicode_patch.dll", "plugins/eu4_unicode_patch/LICENSE.txt", "EU4UnicodePatch.README.txt"}
    if set(main) != expected_main:
        raise ValueError("Unexpected Windows main patch payload")
    if digest(main["plugins/eu4_unicode_patch.dll"]) != validation.get("patch_dll_sha256") or digest(main["VERSION.dll"]) != validation.get("loader_sha256"):
        raise ValueError("Windows patch differs from the tested binaries")
    font_files = archive_files(paths[info["FontPackage"]])
    font_manifest = json.loads(font_files["plugins/eu4_unicode_patch/font-manifest.json"])
    if font_manifest.get("source_commit") != info["SourceCommit"] or font_manifest.get("patch_version") != info["Version"]:
        raise ValueError("Font package identity mismatch")
    native = archive_files(paths[info["LinuxPackage"]])
    internal = json.loads(native["build-info.json"])
    same_identity(internal, info, True)
    plugin_hash = digest(native["libeu4_unicode.so"])
    if plugin_hash != internal.get("patch_sha256") or plugin_hash != linux_info.get("patch_sha256"):
        raise ValueError("Linux archive differs from the tested binary")
    if any("diagnos" in name.lower() for name in native):
        raise ValueError("Diagnostic tools must not be embedded in the main Linux patch")
    # Check every producer-supplied payload checksum, not only the archive hash.
    checksums = native["SHA256SUMS"].decode().splitlines()
    for line in checksums:
        expected, name = line.split(None, 1)
        name = name.strip().removeprefix("./")
        if digest(native[name]) != expected:
            raise ValueError("Linux internal checksum mismatch")
    archive_hash = (linux / "linux-package-sha256.txt").read_text().split()[0]
    if digest(paths[info["LinuxPackage"]].read_bytes()) != archive_hash:
        raise ValueError("Linux producer archive checksum mismatch")
    check_diagnostics(paths[info["WindowsDiagnostics"]], "windows", info, validation["patch_dll_sha256"], validation["loader_sha256"])
    check_diagnostics(paths[info["LinuxDiagnostics"]], "linux", info, plugin_hash)
    output.mkdir(parents=True, exist_ok=True)
    expected_names = set(asset_names(info)) | {"SHA256SUMS.txt", "release-manifest.json"}
    if any(path.name not in expected_names for path in output.iterdir()):
        raise ValueError("Unexpected files in release bundle destination")
    manifest = dict(schema_version=1, source_commit=info["SourceCommit"], release_tag=info["Tag"],
                    patch_version=info["Version"], channel=info["Channel"],
                    validation=dict(windows_tests=windows_tests, linux_tests=linux_tests, game_runtime="not asserted by the release pipeline"), assets={})
    for name, path in paths.items():
        shutil.copy2(path, output / name)
        manifest["assets"][name] = dict(sha256=digest(path.read_bytes()), bytes=path.stat().st_size)
    (output / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    sums = {name: row["sha256"] for name, row in manifest["assets"].items()}
    sums["release-manifest.json"] = digest((output / "release-manifest.json").read_bytes())
    (output / "SHA256SUMS.txt").write_text("".join(f"{hashvalue}  {name}\n" for name, hashvalue in sorted(sums.items())), encoding="utf-8")
    verify_bundle(output, info)
    return manifest


def verify_bundle(directory, info):
    expected = set(asset_names(info)) | {"SHA256SUMS.txt", "release-manifest.json"}
    if {path.name for path in directory.iterdir()} != expected:
        raise ValueError("Release bundle must contain exactly five archives, metadata and checksums")
    manifest = load_json(directory / "release-manifest.json")
    same_identity(manifest, info, True)
    if manifest.get("channel") != info["Channel"] or set(manifest["assets"]) != set(asset_names(info)):
        raise ValueError("Release manifest channel or asset mismatch")
    sums = {}
    for line in (directory / "SHA256SUMS.txt").read_text().splitlines():
        hashvalue, name = line.split("  ", 1)
        if name in sums or name not in expected - {"SHA256SUMS.txt"}:
            raise ValueError("Unexpected or duplicate checksum entry")
        sums[name] = hashvalue
    if set(sums) != expected - {"SHA256SUMS.txt"}:
        raise ValueError("Incomplete release checksums")
    for name, expected_hash in sums.items():
        path = directory / name
        if digest(path.read_bytes()) != expected_hash:
            raise ValueError(f"Release checksum mismatch: {name}")
        if name in manifest["assets"] and manifest["assets"][name] != dict(sha256=expected_hash, bytes=path.stat().st_size):
            raise ValueError("Release manifest size or digest mismatch")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-artifacts", type=Path, required=True)
    parser.add_argument("--linux-artifacts", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--channel", choices=("Release", "Nightly"), default="Release")
    parser.add_argument("--build-date", default="")
    args = parser.parse_args()
    manifest = assemble(args.windows_artifacts, args.linux_artifacts, args.output_directory,
                        release_info(args.channel, args.build_date))
    print(json.dumps(manifest, indent=2))
