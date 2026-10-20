"""Run the delivered read-only tool against isolated valid/broken installations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def inventory(directory):
    return {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob("*") if path.is_file()}


def run(stage, platform, game, output):
    if platform == "linux":
        args = ["bash", str(stage / "diagnose-linux.sh"), str(game), "--output-directory", str(output)]
    else:
        args = ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(stage / "diagnose-windows.ps1"), "-GameDirectory", str(game), "-OutputDirectory", str(output)]
    before = inventory(game)
    env = dict(os.environ)
    env.pop("LD_PRELOAD", None)
    result = subprocess.run(args, capture_output=True, timeout=45, env=env)
    assert inventory(game) == before, "Diagnostics modified the game installation"
    reports = list(output.glob("*.json"))
    assert len(reports) == 1, (result.returncode, result.stdout, result.stderr)
    report = json.loads(reports[0].read_text(encoding="utf-8-sig"))
    assert report["schema_version"] == 1
    assert list(output.glob("*.txt"))
    return result, {check["id"]: check for check in report["checks"]}


parser = argparse.ArgumentParser()
parser.add_argument("--stage", type=Path, required=True)
parser.add_argument("--payload-directory", type=Path)
parser.add_argument("--platform", choices=("windows", "linux"), required=True)
args = parser.parse_args()
stage = args.stage.resolve()
payload = args.payload_directory.resolve() if args.payload_directory else stage.parent
metadata = json.loads((stage / "diagnostic-info.json").read_text(encoding="utf-8"))
for name, digest in metadata["files"].items():
    assert hashlib.sha256((stage / name).read_bytes()).hexdigest() == digest
with tempfile.TemporaryDirectory(prefix="eu4-diagnostic-tests-") as directory:
    root = Path(directory)
    game = root / "Steam Library" / "EU4 诊断"
    game.mkdir(parents=True)
    # First run must report missing files, retain a complete native probe report,
    # and write the report even when no usable game executable exists.
    result, checks = run(stage, args.platform, game, root / "missing")
    assert result.returncode == 1, (result.stdout, result.stderr)
    assert checks["runtime.host_unicode"]["status"] == "pass", checks
    assert checks["game.elf" if args.platform == "linux" else "game.pe"]["status"] == "fail"
    assert checks["runtime.game_loaded_libraries"]["status"] == "info"
    if args.platform == "linux":
        patch = game / ".eu4-unicode"
        patch.mkdir()
        shutil.copy2(payload / "libeu4_unicode.so", patch / "libeu4_unicode.so")
        shutil.copy2(payload / ("build-info.json" if args.payload_directory else "linux-build-info.json"), patch / "build-info.json")
        (patch / "launcher-settings.json.original").write_text(json.dumps(dict(gameId="eu4", exePath="./eu4")))
        (game / "launcher-settings.json").write_text(json.dumps(dict(gameId="eu4", exePath="./.eu4-unicode/eu4-unicode-game.sh")))
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", header, 16, 2, 62)
        (game / "eu4").write_bytes(header)
        (game / "eu4").chmod(0o755)
        result, checks = run(stage, args.platform, game, root / "installed")
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert checks["patch.binary"]["status"] == checks["patch.dependencies"]["status"] == "pass"
        # Steam restoring the original entry must be diagnosed, not repaired.
        (game / "launcher-settings.json").write_text(json.dumps(dict(gameId="eu4", exePath="./eu4")))
        result, checks = run(stage, args.platform, game, root / "steam-restored")
        assert result.returncode == 1 and checks["launcher.integration"]["status"] == "fail"
    else:
        # Real native PE, but deliberately not a game: the checker must refuse it
        # without running its entry point, while still writing the whole report.
        shutil.copy2(stage / "executable_check.exe", game / "eu4.exe")
        (game / "plugins").mkdir()
        shutil.copy2(stage.parent / "eu4_unicode_patch.dll", game / "plugins/eu4_unicode_patch.dll")
        shutil.copy2(stage.parent / "VERSION.dll", game / "VERSION.dll")
        (game / "launcher-settings.json").write_text(json.dumps(dict(gameId="eu4", exePath="./eu4.exe")))
        result, checks = run(stage, args.platform, game, root / "installed")
        assert result.returncode == 1 and checks["game.pe"]["status"] == "pass", (result.returncode, checks, result.stderr.decode(errors="replace"))
        assert checks["game.instructions"]["status"] == "fail"
        assert checks["patch.patch"]["status"] == checks["patch.loader"]["status"] == "pass"
    damaged = root / "Damaged Diagnostic Download"
    shutil.copytree(stage, damaged)
    (damaged / ("diagnostic_probe.exe" if args.platform == "windows" else "diagnostic_probe")).unlink()
    result, checks = run(damaged, args.platform, game, root / "missing-helper")
    assert result.returncode == 1 and checks["runtime.host_unicode"]["status"] == "fail"
print("Independent diagnostic package, explicit failure reports and read-only installation checks passed")
