"""Check real native interfaces and accept non-conflicting ELF changes.

The validation helper substitutes the game main; it does not enter a campaign
or access the player's userdir. The actual engine lexer/string/date/selection
functions execute in the game's ELF. Date/title localization is controlled by
fixture hooks. This is not a GUI or full campaign test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--game", type=Path, required=True)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--library-path", type=Path)
args = parser.parse_args()
game, build, output = args.game.resolve(strict=True), args.build.resolve(strict=True), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
plugin, helper = build / "libeu4_unicode.so", build / "liblinux_native_contract.so"
assert game.name == "eu4" and plugin.is_file() and helper.is_file()
original = game.read_bytes()
source_hash = hashlib.sha256(original).hexdigest()
env = os.environ.copy()
env.update(EU4_UNICODE_NATIVE_CONTRACT="1", LC_ALL="C.UTF-8", LD_PRELOAD=f"{helper}:{plugin}")
libraries = [str(game.parent)]
if args.library_path:
    libraries.insert(0, str(args.library_path.resolve(strict=True)))
if env.get("LD_LIBRARY_PATH"):
    libraries.append(env["LD_LIBRARY_PATH"])
env["LD_LIBRARY_PATH"] = ":".join(libraries)
records = []


def run(name, executable, accepted):
    directory = output / name
    directory.mkdir()
    result = subprocess.run([str(executable)], cwd=directory, env=env,
                            capture_output=True, text=True, timeout=30)
    (directory / "stdout.txt").write_text(result.stdout)
    (directory / "stderr.txt").write_text(result.stderr)
    log = (directory / "eu4_unicode_linux.log").read_text()
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    assert f"Executable SHA256={digest}" in log
    if accepted:
        assert result.returncode == 0, (name, result.returncode, result.stderr, result.stdout)
        assert "PASS all native contract checks" in result.stdout
        assert "Linux native hooks enabled" in log
        assert "Localization imported=4 rejected=0" in log
        assert "Native YAML UTF-8 signatures supplied=2" in log
        assert "Native legacy script tokens=4 sequences=8 rejected=0" in log
    else:
        assert result.returncode == 1, (name, result.returncode, result.stderr)
        assert "Native instruction mismatch" in log
        assert "hooks enabled" not in log and "PASS" not in result.stdout
    records.append(dict(name=name, sha256=digest, accepted=accepted, exit_code=result.returncode))


run("original", game, True)
variant = output / "variant"
variant.mkdir()
modified = variant / "eu4"
modified.write_bytes(original + b"\nEU4 compatible non-code modification\n")
modified.chmod(0o755)
run("non-code-change", modified, True)
phoff = struct.unpack_from("<Q", original, 32)[0]
entry_size, count = struct.unpack_from("<HH", original, 54)
for index in range(count):
    kind, flags, offset, address, _, size, _, _ = struct.unpack_from("<IIQQQQQQ", original, phoff + index * entry_size)
    if kind == 1 and address <= 0x255E44E < address + size:
        file_offset = offset + 0x255E44E - address
        break
else:
    raise ValueError("Native lexer address is outside ELF load segments")
with modified.open("r+b") as stream:
    stream.seek(file_offset)
    stream.write(bytes([original[file_offset] ^ 1]))
run("instruction-conflict", modified, False)
assert game.read_bytes() == original
report = dict(game=str(game), original_sha256=source_hash,
              plugin_sha256=hashlib.sha256(plugin.read_bytes()).hexdigest(),
              source_unchanged=True, full_game_ui_test=False, cases=records)
(output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
