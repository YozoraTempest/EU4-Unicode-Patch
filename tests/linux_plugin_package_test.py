"""Run the actual plugin package against the supported runtime's libraries."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

archive, build = (Path(value).resolve() for value in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="eu4-unicode-package-") as temporary:
    game = Path(temporary) / "game with spaces"
    game.mkdir()
    with zipfile.ZipFile(archive) as package:
        assert package.testzip() is None
        assert set(package.namelist()) == {
            "plugin/libeu4_unicode.so", "plugin/eu4_unicode/config.ini",
            "plugin/eu4_unicode/使用说明.txt", "plugin/eu4_unicode/LICENSE",
        }, package.namelist()
        package.extractall(game)
    environment = os.environ.copy()
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "SDL_DYNAMIC_API"):
        environment.pop(key, None)
    environment["LC_ALL"] = "C"
    # Resources are beside the plugin; all text and input libraries must be
    # supplied by the runtime, with no package-specific library search path.
    for name in ("unicode_services_tests", "unicode_layout_tests", "shaped_paragraph_tests"):
        target = game / "plugin" / name
        shutil.copy2(build / name, target)
        subprocess.run([str(target)], cwd=game, env=environment, check=True, timeout=30)
    target = game / "plugin/unicode_layout_tests"
    # Pango can already be loaded globally, and unrelated plugins can export
    # colliding symbols. Both must preserve the runtime's rendering functions.
    collision = game / "plugin/libcollision.so"
    shutil.copy2(build / "liblinux_render_collision.so", collision)
    for preload in ("libpangocairo-1.0.so.0", "./plugin/libcollision.so"):
        result = subprocess.run([str(target)], cwd=game, env=environment | {"LD_PRELOAD": preload},
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        assert "cannot be preloaded" not in result.stderr, result.stderr
    shutil.copy2(build / "loader-fixture/eu4", game / "eu4")
    result = subprocess.run(["./eu4"], cwd=game,
        env=environment | {"LD_PRELOAD": "./plugin/libeu4_unicode.so"}, capture_output=True, text=True, timeout=15)
    assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
    log = (game / "eu4_unicode_linux.log").read_text()
    assert "Refused: Native instruction outside executable mapping" in log, log
    assert "Cannot find a matching" not in log and "runtime is missing" not in log, log
print("Four-file package, runtime ICU/rendering, symbol collisions and game entry checks passed")
