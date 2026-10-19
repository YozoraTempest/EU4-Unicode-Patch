"""Install the native game wrapper called by the Paradox Launcher."""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile

WRAPPER = "./.eu4-unicode/eu4-unicode-game.sh"
PAYLOAD = ("libeu4_unicode.so", "launch-linux.sh", "eu4-unicode-game.sh")


def read_settings(path):
    raw = path.read_bytes()
    data = json.loads(raw.decode("utf-8-sig"))
    if not isinstance(data, dict) or not isinstance(data.get("exePath"), str):
        raise ValueError(f"Missing exePath in {path}")
    return raw, data


def atomic_write(path, content):
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
        temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def encode_settings(data):
    return (json.dumps(data, ensure_ascii=False, indent="\t") + "\n").encode("utf-8")


def install(package, game, settings, destination, backup):
    executable = game / "eu4"
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f"Native eu4 executable not found or not executable: {executable}")
    with executable.open("rb") as stream:
        native_header = stream.read(5)
    if native_header != b"\x7fELF\x02":
        raise ValueError(f"Expected the native Linux ELF64 executable: {executable}")
    for name in PAYLOAD:
        if not (package / name).is_file():
            raise ValueError(f"Missing {name} beside install-paradox-launcher.sh")
    with (package / "libeu4_unicode.so").open("rb") as stream:
        plugin_header = stream.read(5)
    if plugin_header != b"\x7fELF\x02":
        raise ValueError("libeu4_unicode.so is not an ELF64 library")

    original, data = read_settings(settings)
    if data.get("gameId") != "eu4":
        raise ValueError(f"Expected EU4 launcher settings: {settings}")
    # Preserve the existing, published installer's backup when upgrading it.
    # An unknown original entry cannot be reconstructed from the wrapper.
    if data["exePath"] == WRAPPER and not backup.is_file():
        raise ValueError("Launcher integration has no original settings backup")
    if backup.exists():
        _, saved = read_settings(backup)
        if saved.get("gameId") != "eu4" or saved["exePath"] == WRAPPER:
            raise ValueError("The settings backup already points to the wrapper")

    destination.mkdir(exist_ok=True)
    if not backup.exists():
        with backup.open("xb") as stream:
            stream.write(original)
    for name in PAYLOAD:
        target = destination / name
        content = (package / name).read_bytes()
        if not target.is_file() or target.read_bytes() != content:
            atomic_write(target, content)
        target.chmod(0o755 if name.endswith(".sh") else 0o644)
    if (package / "LICENSE").is_file():
        atomic_write(destination / "LICENSE", (package / "LICENSE").read_bytes())
    if (package / "licenses").is_dir() and package != destination:
        shutil.copytree(package / "licenses", destination / "licenses", dirs_exist_ok=True)
    if data["exePath"] != WRAPPER:
        data["exePath"] = WRAPPER
        atomic_write(settings, encode_settings(data))
    print(f"Installed Paradox Launcher integration: {settings}")
    print("Start EU4 normally through Steam and the Paradox Launcher.")
    print("Leave Steam launch options empty, or use start-paradox-launcher.sh for automatic repair.")
    print("After Steam verification, run this installer again to restore the integration.")


def uninstall(settings, destination, backup):
    if not backup.is_file():
        raise ValueError(f"Original launcher settings backup not found: {backup}")
    original, saved = read_settings(backup)
    _, data = read_settings(settings)
    if saved.get("gameId") != "eu4" or saved["exePath"] == WRAPPER:
        raise ValueError("The settings backup already points to the wrapper")
    if data["exePath"] == WRAPPER:
        data["exePath"] = saved["exePath"]
        # Preserve exact original bytes when no other setting has changed.
        atomic_write(settings, original if data == saved else encode_settings(data))
    for name in PAYLOAD:
        (destination / name).unlink(missing_ok=True)
    # Keep the backup and notices so reinstall/Steam repair cannot destroy
    # the original entry or another installed tool's files.
    print(f"Restored Paradox Launcher game entry: {settings}")
    print(f"Original settings backup retained: {backup}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("install", "uninstall"))
    parser.add_argument("game_directory", nargs="?", default=os.environ.get("EU4_GAME_DIR", "."),
                        help="Directory containing eu4 and launcher-settings.json")
    args = parser.parse_args()
    package = Path(__file__).resolve().parent
    game = Path(args.game_directory).expanduser().resolve(strict=True)
    settings = game / "launcher-settings.json"
    destination = game / ".eu4-unicode"
    backup = destination / "launcher-settings.json.original"
    if not settings.is_file() or settings.is_symlink():
        raise ValueError(f"Expected a regular launcher-settings.json: {settings}")
    if destination.is_symlink() or backup.is_symlink():
        raise ValueError("The installation directory and backup must not be symlinks")
    if args.action == "install":
        install(package, game, settings, destination, backup)
    else:
        uninstall(settings, destination, backup)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
