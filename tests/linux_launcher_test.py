"""Exercise installation, game-only preload, argument forwarding and repair."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile

plugin, fixture, tools = (Path(value).resolve() for value in sys.argv[1:])
wrapper = "./.eu4-unicode/eu4-unicode-game.sh"
scripts = ("launch-linux.sh", "eu4-unicode-game.sh", "linux-launcher.py",
           "install-paradox-launcher.sh", "uninstall-paradox-launcher.sh",
           "start-paradox-launcher.sh")
env = os.environ.copy()
env.pop("LD_PRELOAD", None)
env["LC_ALL"] = "C"


def run(argv, cwd, code=0):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=15)
    assert result.returncode == code, (argv, result.returncode, result.stdout, result.stderr)
    assert "cannot be preloaded" not in result.stderr, result.stderr
    return result


with tempfile.TemporaryDirectory(prefix="eu4-paradox-launcher-") as temporary:
    root = Path(temporary)
    # Both the package and Steam game directory may contain spaces in this mode.
    package = root / "Downloaded Linux Package"
    game = root / "Steam Library" / "Europa Universalis IV"
    package.mkdir()
    game.mkdir(parents=True)
    shutil.copyfile(plugin, package / "libeu4_unicode.so")
    for name in scripts:
        shutil.copyfile(tools / name, package / name)
        (package / name).chmod(0o755)
    (package / "config.ini").write_text("[search]\ntypo_tolerance=0\n")
    (package / "pinyin.txt").write_text("𠮷野: ji ye\n")
    # The helper's real executable remains bash, so the real preloader must
    # forward it. Bash options also catch any injected -skiplauncher argument.
    (game / "eu4").symlink_to(shutil.which("bash"))
    probe = "import json,os,sys; print(json.dumps({'args':sys.argv[1:],'cwd':os.getcwd(),'preload':os.environ.get('LD_PRELOAD'),'locale':os.environ.get('LC_ALL')}))"
    probe_script = f'exec {shlex.quote(sys.executable)} -c {shlex.quote(probe)} "$@"'
    initial = {"gameId": "eu4", "exePath": "./eu4", "exeArgs": ["-c", probe_script, "--"],
               "gameDataPath": "$LINUX_DATA_HOME/Paradox Interactive/Europa Universalis IV",
               "displayName": "Europa Universalis IV", "version": "1.37.5"}
    original = (json.dumps(initial, ensure_ascii=False) + "\n").encode()
    settings = game / "launcher-settings.json"
    settings.write_bytes(original)
    backup = game / ".eu4-unicode/launcher-settings.json.original"
    install = [package / "install-paradox-launcher.sh", game]
    uninstall = [package / "uninstall-paradox-launcher.sh", game]
    run(install, root)
    configured = json.loads(settings.read_text())
    expected = dict(initial, exePath=wrapper)
    assert configured == expected, configured
    assert backup.read_bytes() == original
    assert (game / ".eu4-unicode/config.ini").read_bytes() == (package / "config.ini").read_bytes()
    assert (game / ".eu4-unicode/pinyin.txt").read_bytes() == (package / "pinyin.txt").read_bytes()
    custom_config = "[search]\nfuzzy_pinyin=1\n"
    (game / ".eu4-unicode/config.ini").write_text(custom_config)
    mtimes = {p: p.stat().st_mtime_ns for p in [settings, backup, game / ".eu4-unicode/libeu4_unicode.so"]}
    run(install, root)
    assert (game / ".eu4-unicode/config.ini").read_text() == custom_config
    assert mtimes == {p: p.stat().st_mtime_ns for p in mtimes}

    arguments = ["-enabletelemetry", "-gdpr-compliant", "-userdir=/test user data", "-mod=mod/中文.mod"]
    result = run([game / configured["exePath"], *configured["exeArgs"], *arguments], root)
    captured = json.loads(result.stdout)
    assert captured == {"args": arguments, "cwd": str(game),
                        "preload": "./.eu4-unicode/libeu4_unicode.so", "locale": "C.UTF-8"}, captured

    # The outer Steam command receives no patch preload and is still run.
    command = [package / "start-paradox-launcher.sh", game, "--", sys.executable, "-c", probe, "dowser"]
    outer = json.loads(run(command, root).stdout.splitlines()[-1])
    assert outer["args"] == ["dowser"] and outer["preload"] is None, outer
    run([package / "start-paradox-launcher.sh", game, "--", shutil.which("false")], root, code=1)

    # Steam validation restores the official settings; the Steam option repairs it.
    settings.write_bytes(original)
    run(command, root)
    assert json.loads(settings.read_text())["exePath"] == wrapper
    assert backup.read_bytes() == original

    # A real ELF with the eu4 name proves loading even through spaced paths.
    (game / "eu4").unlink()
    shutil.copyfile(fixture, game / "eu4")
    (game / "eu4").chmod(0o755)
    real = run([game / wrapper, *arguments], root, code=1)
    assert "fixture main called" not in real.stdout
    log = (game / "eu4_unicode_linux.log").read_text()
    assert "Executable SHA256=" + hashlib.sha256(fixture.read_bytes()).hexdigest() in log
    assert "Refused: Native instruction outside executable mapping" in log, log

    # Uninstall restores original bytes; if the launcher updated another field,
    # restore only exePath so that new settings are preserved.
    run(uninstall, root)
    assert settings.read_bytes() == original
    run(install, root)
    changed = json.loads(settings.read_text())
    changed["version"] = "new launcher version"
    settings.write_text(json.dumps(changed))
    run(uninstall, root)
    restored = json.loads(settings.read_text())
    assert restored == dict(initial, version="new launcher version"), restored
    assert backup.read_bytes() == original
    assert not (game / wrapper).exists()

    # An invalid settings document must be left intact by the installer.
    settings.write_bytes(b"{not valid JSON")
    run(install, root, code=1)
    assert settings.read_bytes() == b"{not valid JSON"

print("Paradox Launcher install, game-only preload, argument preservation, spaced paths, Steam repair and uninstall passed")
