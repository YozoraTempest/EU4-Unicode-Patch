#!/usr/bin/env python3
"""Read-only EU4 installation checks; reports are the only files written."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent


def command(argv, timeout=20, cwd=None):
    env = dict(os.environ)
    env.pop("LD_PRELOAD", None)
    try:
        result = subprocess.run([str(value) for value in argv], capture_output=True, text=True,
                                errors="replace", timeout=timeout, env=env, cwd=cwd)
        return result.returncode, result.stdout, result.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        return -1, "", str(error)


def steam_games():
    home = Path.home()
    candidates = [Path.cwd(), ROOT, ROOT.parent]
    if os.environ.get("EU4_GAME_DIR"):
        candidates.append(Path(os.environ["EU4_GAME_DIR"]).expanduser())
    for steam in [home / ".local/share/Steam", home / ".steam/steam",
                  home / ".var/app/com.valvesoftware.Steam/.local/share/Steam"]:
        libraries = [steam]
        vdf = steam / "steamapps/libraryfolders.vdf"
        if vdf.is_file():
            for path in re.findall(r'"path"\s*"([^"\n]+)"', vdf.read_text(errors="replace")):
                libraries.append(Path(path.replace("\\\\", "\\")))
        candidates.extend(path / "steamapps/common/Europa Universalis IV" for path in libraries)
    return sorted({path.resolve() for path in candidates if (path / "eu4").is_file()})


def diagnose(game, patch, info):
    checks = []
    def add(name, status, detail, remedy=""):
        checks.append(dict(id=name, status=status, detail=detail, remedy=remedy))
    system = platform.freedesktop_os_release() if hasattr(platform, "freedesktop_os_release") else {}
    distro = system.get("ID", "")
    families = system.get("ID_LIKE", "").split()
    install = "Install ICU, Pango, Cairo, Fontconfig, libstdc++ and an SDL2 ABI library for this runtime."
    if distro in ("fedora", "rhel", "centos") or "fedora" in families:
        install = "sudo dnf install python3 libstdc++ libicu pango cairo fontconfig sdl2-compat"
        fonts = "sudo dnf install google-noto-sans-cjk-fonts google-noto-sans-arabic-fonts google-noto-sans-hebrew-fonts google-noto-sans-devanagari-fonts google-noto-sans-thai-fonts"
    elif distro == "arch" or "arch" in families:
        install = "sudo pacman -S --needed icu pango cairo fontconfig gcc-libs sdl2-compat"
        fonts = "sudo pacman -S --needed noto-fonts noto-fonts-cjk"
    elif distro in ("ubuntu", "debian") or "debian" in families:
        install = "sudo apt install python3 libicu-dev libpango-1.0-0 libcairo2 libfontconfig1 libstdc++6 libsdl2-2.0-0"
        fonts = "sudo apt install fonts-noto-core fonts-noto-cjk"
    else:
        fonts = "Install fonts covering the reported codepoints; restart the game."
    add("host.architecture", "pass" if platform.machine() == "x86_64" else "fail", platform.machine(), "Use native Linux x86_64 EU4 1.37.5.")
    if game is None:
        add("game.directory", "fail", "No unique EU4 installation was selected.", 'Run diagnose-linux.sh "/path/to/Europa Universalis IV".')
    else:
        exe = game / "eu4"
        try:
            with exe.open("rb") as stream:
                header = stream.read(20)
                checksum = hashlib.sha256(header)
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    checksum.update(block)
                digest = checksum.hexdigest()
            valid = len(header) == 20 and header[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<HH", header, 16) == (2, 62)
            add("game.elf", "pass" if valid and os.access(exe, os.X_OK) else "fail",
                dict(path=str(exe), sha256=digest, elf64_x86_64_et_exec=valid),
                "Select native Linux EU4, disable Proton for this installation, and verify executable permissions.")
        except OSError as error:
            add("game.elf", "fail", str(error), "Select the directory containing the native eu4 executable.")
        settings = game / "launcher-settings.json"
        try:
            data = json.loads(settings.read_text(encoding="utf-8-sig"))
            wrappers = {name: (game / ".eu4-unicode" / name).is_file() and os.access(game / ".eu4-unicode" / name, os.X_OK)
                        for name in ("eu4-unicode-game.sh", "launch-linux.sh")}
            integrated = data.get("gameId") == "eu4" and data.get("exePath") == "./.eu4-unicode/eu4-unicode-game.sh" and all(wrappers.values())
            add("launcher.integration", "pass" if integrated else "fail", dict(exePath=data.get("exePath"), executable_wrappers=wrappers),
                "Run install-paradox-launcher.sh from the patch download, or use start-paradox-launcher.sh for Steam repair.")
            backup = game / ".eu4-unicode/launcher-settings.json.original"
            saved = json.loads(backup.read_text(encoding="utf-8-sig")) if backup.is_file() else {}
            usable = saved.get("gameId") == "eu4" and isinstance(saved.get("exePath"), str) and saved["exePath"] != "./.eu4-unicode/eu4-unicode-game.sh"
            add("launcher.backup", "pass" if usable else "fail", str(backup), "Restore official launcher settings through Steam, then reinstall the patch integration.")
        except (OSError, ValueError, AttributeError) as error:
            add("launcher.integration", "fail", str(error), "Verify the EU4 installation and reinstall launcher integration.")
    if patch is not None:
        library = patch / "libeu4_unicode.so"
        try:
            digest = hashlib.sha256(library.read_bytes()).hexdigest()
            expected = info.get("patch_sha256")
            add("patch.binary", "pass" if digest == expected else "warn", dict(path=str(library), sha256=digest, expected=expected),
                "Use diagnostics from the same release to identify the build; a hash difference alone does not prove incompatibility.")
            metadata = patch / "build-info.json"
            add("patch.version", "pass" if metadata.is_file() else "warn",
                json.loads(metadata.read_text(encoding="utf-8-sig")) if metadata.is_file() else "Installed package has no build metadata.")
            if shutil.which("ldd"):
                code, output, error = command(["ldd", library])
                add("patch.dependencies", "fail" if code or "not found" in output + error else "pass", (output + error).strip(), install)
            else:
                add("patch.dependencies", "warn", "ldd is unavailable; ELF dependencies were not inspected.", install)
        except (OSError, ValueError) as error:
            add("patch.binary", "fail", str(error), "Install the full Linux patch package before diagnosing its dependencies.")
    else:
        add("patch.binary", "fail", "No patch directory selected.", "Install the Linux patch or pass --patch-directory.")
    probe = ROOT / "diagnostic_probe"
    code, output, error = command([probe])
    try:
        native = json.loads(output) if code == 0 else None
        if native is None or native.get("unicode_services") is not True:
            raise ValueError(error or output or f"probe exit code {code}")
        add("runtime.host_unicode", "pass", dict(scope="diagnostic process; not proof of game loaded libraries", libraries=native["libraries"]))
        missing = [row for row in native["fonts"] if not row["covered"]]
        add("fonts.system_coverage", "warn" if missing else "pass", native["fonts"], fonts)
    except (ValueError, KeyError) as error:
        add("runtime.host_unicode", "fail", str(error), install)
    # Probe SDL in a child interpreter: do not initialize a window or IME.
    sdl_probe = '''import ctypes,ctypes.util,json,os
name=os.environ.get("SDL_DYNAMIC_API") or ctypes.util.find_library("SDL2-2.0")
if not name: raise RuntimeError("SDL2 ABI library not found")
lib=ctypes.CDLL(name)
class Version(ctypes.Structure): _fields_=[("major",ctypes.c_uint8),("minor",ctypes.c_uint8),("patch",ctypes.c_uint8)]
v=Version();lib.SDL_GetVersion.argtypes=[ctypes.POINTER(Version)];lib.SDL_GetVersion(ctypes.byref(v))
paths=sorted({line.split(None,5)[-1].strip() for line in open("/proc/self/maps") if "libSDL" in line})
print(json.dumps({"requested":name,"version":[v.major,v.minor,v.patch],"loaded_paths":paths}))
'''
    import sys
    code, output, error = command([sys.executable, "-c", sdl_probe], cwd=game if game is not None and game.is_dir() else None)
    try:
        sdl = json.loads(output) if code == 0 else None
        good = sdl is not None and sdl.get("version", [0])[0] == 2
        add("runtime.host_sdl", "pass" if good else "fail", sdl or error.strip(), install)
    except ValueError:
        add("runtime.host_sdl", "fail", error or output, install)
    live = []
    if game is not None:
        for process in Path("/proc").iterdir():
            if not process.name.isdecimal():
                continue
            try:
                if (process / "exe").resolve() != (game / "eu4").resolve():
                    continue
                paths = sorted({line.split(None, 5)[-1] for line in (process / "maps").read_text().splitlines()
                                if any(name in line for name in ("libeu4_unicode", "libicu", "libSDL", "libpango", "libcairo"))})
                live.append(dict(pid=int(process.name), libraries=paths))
            except OSError:
                continue
    add("runtime.game_loaded_libraries", "pass" if live else "info", live or "No readable matching game process; actual game library paths are unverified.")
    if game is not None:
        logfile = game / "eu4_unicode_linux.log"
        if logfile.is_file():
            with logfile.open("rb") as stream:
                stream.seek(max(0, logfile.stat().st_size - 65536))
                lines = stream.read().decode("utf-8", errors="replace").splitlines()
            selected = [line for line in lines if any(word in line for word in ("Refused:", "hooks enabled", "custom-dictionary", "Executable SHA"))][-12:]
            refused = any("Refused:" in line for line in selected)
            add("patch.last_log", "warn" if refused else "info", dict(modified=logfile.stat().st_mtime, lines=selected),
                "This is an existing log, not a fresh launch result. Review any Refused message and its timestamp.")
    return dict(schema_version=1, generated_at=datetime.now(timezone.utc).isoformat(), tool=info,
                host=dict(os=system, architecture=platform.machine(), libc=platform.libc_ver(),
                          session={key:os.environ.get(key) for key in ("XDG_SESSION_TYPE", "GTK_IM_MODULE", "QT_IM_MODULE", "LC_ALL", "LANG", "SDL_DYNAMIC_API")}),
                game_directory=str(game) if game else None, checks=checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_directory", nargs="?")
    parser.add_argument("--patch-directory")
    parser.add_argument("--output-directory", default=".")
    args = parser.parse_args()
    candidates = steam_games() if not args.game_directory else []
    game = Path(args.game_directory).expanduser().resolve() if args.game_directory else (candidates[0] if len(candidates) == 1 else None)
    patch = Path(args.patch_directory).expanduser().resolve() if args.patch_directory else (game / ".eu4-unicode" if game else None)
    info = json.loads((ROOT / "diagnostic-info.json").read_text(encoding="utf-8-sig"))
    report = diagnose(game, patch, info)
    report["detected_installations"] = [str(path) for path in candidates]
    output = Path(args.output_directory).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    stem = "eu4-diagnostics-linux-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    jsonfile = output / (stem + ".json")
    jsonfile.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = [f"EU4 diagnostics {info['release_tag']}", f"Game: {game}"]
    for check in report["checks"]:
        lines.append(f"[{check['status'].upper()}] {check['id']}: {check['detail']}")
        if check["status"] in ("warn", "fail") and check["remedy"]:
            lines.append("  " + check["remedy"])
    lines.append(f"JSON report: {jsonfile}")
    (output / (stem + ".txt")).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines))
    return int(any(check["status"] == "fail" for check in report["checks"]))


if __name__ == "__main__":
    raise SystemExit(main())
