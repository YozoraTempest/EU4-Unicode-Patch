"""Shared release identity and asset names for both platform builders."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def release_info(channel="Release", build_date="", root=ROOT):
    version = (root / "VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("Invalid VERSION")
    commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Invalid source commit")
    if channel == "Nightly":
        if not build_date:
            # A fixed UTC offset avoids requiring an IANA timezone database on Windows.
            from datetime import timezone, timedelta
            build_date = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d")
        if not re.fullmatch(r"\d{8}", build_date):
            raise ValueError("Build date must be yyyyMMdd")
        datetime.strptime(build_date, "%Y%m%d")
        suffix = f"nightly-{build_date}-{commit[:7]}"
        tag = f"v{version}-{suffix}"
    elif channel == "Release" and not build_date:
        suffix = tag = f"v{version}"
    else:
        raise ValueError("Invalid channel/build date combination")
    return dict(Version=version, Channel=channel.lower(), Tag=tag, SourceCommit=commit,
                BuildDate=build_date, PlayerPackage=f"EU4UnicodePatch-1.37.5-{suffix}.zip",
                FontPackage=f"EU4UnicodePatch-fonts-{suffix}.zip",
                LinuxPackage=f"EU4UnicodePatch-linux-x86_64-{suffix}.tar.gz",
                WindowsDiagnostics=f"EU4UnicodePatch-diagnostics-windows-x86_64-{suffix}.zip",
                LinuxDiagnostics=f"EU4UnicodePatch-diagnostics-linux-x86_64-{suffix}.tar.gz")


def asset_names(info):
    return [info[key] for key in ("PlayerPackage", "FontPackage", "LinuxPackage",
                                  "WindowsDiagnostics", "LinuxDiagnostics")]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", choices=("Release", "Nightly"), default="Release")
    parser.add_argument("--build-date", default="")
    args = parser.parse_args()
    print(json.dumps(release_info(args.channel, args.build_date)))
