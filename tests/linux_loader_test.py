"""Verify that the real preload entry forwards helpers and rejects unknown ELF."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

plugin, fixture = (Path(value).resolve() for value in sys.argv[1:])
assert plugin.is_file() and fixture.name == "eu4" and fixture.is_file()
env = os.environ.copy()
env["LD_PRELOAD"] = str(plugin)
with tempfile.TemporaryDirectory(prefix="eu4-linux-loader-") as directory:
    helper = subprocess.run(["/usr/bin/true"], cwd=directory, env=env,
                            capture_output=True, timeout=10)
    assert helper.returncode == 0, helper.stderr
    assert not (Path(directory) / "eu4_unicode_linux.log").exists()
    refused = subprocess.run([str(fixture)], cwd=directory, env=env,
                             capture_output=True, timeout=10)
    assert refused.returncode == 1, (refused.returncode, refused.stderr)
    assert b"fixture main called" not in refused.stdout
    log = (Path(directory) / "eu4_unicode_linux.log").read_text()
    assert "Executable SHA256=" in log
    assert "Refused: unsupported executable" in log
    assert "hooks enabled" not in log
print("Native preload forwarding and unknown-ELF refusal passed")
