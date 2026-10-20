"""Bind the generic binary to its source and test report without shipping diagnostics."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from release_info import ROOT, release_info

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build-directory", type=Path, required=True)
parser.add_argument("--channel", choices=("Release", "Nightly"), default="Release")
parser.add_argument("--build-date", default="")
parser.add_argument("--excluded-tests", default="")
args = parser.parse_args()
info = release_info(args.channel, args.build_date)
report = dict(schema_version=1, source_commit=info["SourceCommit"], patch_version=info["Version"],
              release_tag=info["Tag"], channel=info["Channel"],
              source_tree_dirty=bool(subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True)),
              patch_sha256=hashlib.sha256((args.build_directory / "libeu4_unicode.so").read_bytes()).hexdigest(),
              excluded_tests=args.excluded_tests,
              ctest_sha256=hashlib.sha256((args.build_directory / "ctest-linux-results.xml").read_bytes()).hexdigest())
(args.build_directory / "linux-build-info.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
