#!/usr/bin/env python3
"""Package the x86_64 Augustus plugin built in Valve's sniper SDK."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_BUILD = "3.0.20261007.263965"
AUGUSTUS_SHA256 = "01794f6cb8ce8234a156cbde0b62d9bb2afb2b57d7de08b1e063eca3e24e0e68"
# Rendering, ICU and SDL are supplied by the supported Steam Runtime. The
# SDK's GCC 14 wrapper links its newer C++ and compiler runtimes statically.
ALLOWED_NEEDED = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
                  "librt.so.1", "ld-linux-x86-64.so.2"}


def command(*args):
    environment = os.environ.copy()
    environment.pop("LD_PRELOAD", None)
    environment["LC_ALL"] = "C"
    return subprocess.check_output(args, text=True, env=environment).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/EU4UnicodePatch-Linux-Augustus.zip")
    parser.add_argument("--launcher", type=Path, required=True, help="Augustus-Launcher v0.1.0 release ZIP")
    args = parser.parse_args()
    release = dict(re.findall(r'^([A-Z_]+)=["\']?([^"\'\n]+)', Path("/etc/os-release").read_text(), re.MULTILINE))
    if (release.get("ID") != "steamrt" or release.get("VERSION_CODENAME") != "sniper"
            or release.get("VARIANT") != "SDK" or release.get("BUILD_ID") != RUNTIME_BUILD):
        parser.error("Build and package in the pinned Steam Runtime sniper SDK " + RUNTIME_BUILD)

    plugin = args.build / "libeu4_unicode.so"
    header = plugin.read_bytes()[:20]
    if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        parser.error("Expected an x86_64 ELF plugin")
    dynamic = command("readelf", "-d", str(plugin))
    needed = set(re.findall(r"NEEDED.*\[(.+?)\]", dynamic))
    if needed - ALLOWED_NEEDED:
        parser.error("Unexpected dynamic dependencies: " + ", ".join(sorted(needed - ALLOWED_NEEDED)))
    if re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        parser.error("The plugin must not contain build-machine library paths")
    version_info = command("readelf", "--version-info", str(plugin))
    versions = re.findall(r"\bGLIBC_(\d+)\.(\d+)", version_info)
    if versions and max(tuple(map(int, version)) for version in versions) > (2, 31):
        parser.error("The plugin exceeds sniper's glibc 2.31 ABI baseline")
    exports = {line.split()[-1] for line in command("nm", "-D", "--defined-only", str(plugin)).splitlines()}
    if exports != {"__libc_start_main"}:
        parser.error("Unexpected exported ABI: " + ", ".join(sorted(exports)))

    notices = [(ROOT / "LICENSE").read_text()]
    for relative in ("vendor/phrase-pinyin-data/LICENSE", "vendor/safetyhook/LICENSE", "vendor/picosha2/LICENSE",
                     "build/_deps/zydis-src/LICENSE", "build/_deps/zydis-src/dependencies/zycore/LICENSE"):
        path = ROOT / relative if not relative.startswith("build/") else args.build / relative[6:]
        notices.append("\n" + "=" * 72 + "\n" + relative + "\n" + path.read_text())
    # GCC's runtime exception permits static linking. Preserve the installed
    # toolchain's copyright and exception when distributing those components.
    compiler = Path(command("g++-14", "-print-file-name=libstdc++.a"))
    owner = command("dpkg-query", "-S", str(compiler)).split(": ", 1)[0]
    copyright_file = Path("/usr/share/doc") / owner.split(":")[0] / "copyright"
    notices.append("\n" + "=" * 72 + "\nGCC 14 / libstdc++ runtime\n" + copyright_file.read_text())
    exception = Path("/usr/share/common-licenses/GCC-Runtime-Library-Exception-3.1")
    if exception.is_file():
        notices.append("\n" + exception.read_text())

    files = {
        "plugin/libeu4_unicode.so": plugin.read_bytes(),
        "plugin/eu4_unicode/config.ini": (ROOT / "docs/linux-config.ini").read_bytes(),
        "plugin/eu4_unicode/使用说明.txt": (ROOT / "docs/linux-plugin.txt").read_bytes(),
        "plugin/eu4_unicode/LICENSE": "\n".join(notices).encode("utf-8"),
    }
    if hashlib.sha256(args.launcher.read_bytes()).hexdigest() != AUGUSTUS_SHA256:
        parser.error("Expected the pinned Augustus-Launcher v0.1.0 release package")
    with zipfile.ZipFile(args.launcher) as launcher:
        if launcher.testzip() is not None or set(launcher.namelist()) != {
            "eu4-plugin-loader", "launch.sh", "使用说明.txt", "LICENSE", "plugin/",
        }:
            parser.error("Unexpected Augustus-Launcher package contents")
        for name in ("eu4-plugin-loader", "launch.sh", "使用说明.txt", "LICENSE"):
            files[name] = launcher.read(name)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files.items():
            entry = zipfile.ZipInfo(name)
            entry.create_system = 3
            entry.external_attr = (0o100755 if name in ("eu4-plugin-loader", "launch.sh") else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    print(args.output)
    print("Files:", len(files), "; Augustus-Launcher: 0.1.0; bundled runtime libraries: 0")
    print("NEEDED:", ", ".join(sorted(needed)))
    print("SHA256:", hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
