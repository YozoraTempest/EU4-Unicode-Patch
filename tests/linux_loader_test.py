"""Verify that the real preload entry forwards helpers and rejects unknown ELF."""
import os
import hashlib
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

plugin, fixture, launcher = (Path(value).resolve() for value in sys.argv[1:])
assert plugin.is_file() and fixture.name == "eu4" and fixture.is_file()
env = os.environ.copy()
env["LD_PRELOAD"] = str(plugin)
exports=subprocess.check_output(["nm","-D","--defined-only",str(plugin)],text=True)
assert [line.split()[-1] for line in exports.splitlines()]==["__libc_start_main"],exports
fixture_exports=subprocess.check_output(["nm","-D","--defined-only",str(fixture)],text=True)
assert "EVP_DigestInit_ex" in fixture_exports and "EVP_MD_CTX_free" in fixture_exports
with tempfile.TemporaryDirectory(prefix="eu4-linux-loader-") as directory:
    helper = subprocess.run([shutil.which("true")], cwd=directory, env=env,
                            capture_output=True, timeout=10)
    assert helper.returncode == 0, helper.stderr
    assert not (Path(directory) / "eu4_unicode_linux.log").exists()
    original=fixture.read_bytes()
    for padding in [0,1,63,64,65,65537]:
        case=Path(directory)/str(padding)
        case.mkdir()
        executable=case/"eu4"
        payload=original+bytes(index%256 for index in range(padding))
        executable.write_bytes(payload)
        executable.chmod(0o755)
        refused = subprocess.run([str(executable)], cwd=case, env=env,
                                 capture_output=True, timeout=10)
        assert refused.returncode == 1, (refused.returncode, refused.stderr)
        assert b"fixture main called" not in refused.stdout
        log = (case / "eu4_unicode_linux.log").read_text()
        assert "Executable SHA256="+hashlib.sha256(payload).hexdigest() in log
        assert "Refused: Native instruction outside executable mapping" in log, log
        assert "hooks enabled" not in log
    package=Path(directory)/"package"
    package.mkdir()
    shutil.copyfile(plugin,package/"libeu4_unicode.so")
    shutil.copyfile(launcher,package/"launch-linux.sh")
    (package/"launch-linux.sh").chmod(0o755)
    game=Path(directory)/"steam game"
    game.mkdir()
    shutil.copyfile(fixture,game/"eu4")
    (game/"eu4").chmod(0o755)
    steam=subprocess.run([str(package/"launch-linux.sh"),"--steam",str(game/"eu4"),
        "-debug","--",shutil.which("false")],cwd=package,capture_output=True,timeout=10)
    assert steam.returncode==1,(steam.returncode,steam.stderr)
    log=(game/"eu4_unicode_linux.log").read_text()
    assert "Executable SHA256="+hashlib.sha256(original).hexdigest() in log
    assert not (package/"eu4_unicode_linux.log").exists()
    direct=subprocess.run([str(package/"launch-linux.sh"),shutil.which("true")],
        cwd=package,capture_output=True,timeout=10)
    assert direct.returncode==0,direct.stderr
    for locale in ["C","POSIX","C.UTF-8"]:
        locale_env=os.environ.copy()
        locale_env["LC_ALL"]=locale
        locale_probe=subprocess.run([str(package/"launch-linux.sh"),"/usr/bin/locale","charmap"],
            cwd=package,env=locale_env,capture_output=True,text=True,timeout=10)
        assert locale_probe.returncode==0,(locale,locale_probe.stderr)
        assert locale_probe.stdout.strip()=="UTF-8",(locale,locale_probe.stdout)
print("Native preload forwarding, crypto-symbol isolation and native image/instruction refusal passed")
