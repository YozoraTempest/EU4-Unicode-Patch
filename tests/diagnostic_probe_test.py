"""Validate real platform font/Unicode diagnostics without requiring all fonts."""
import json
import subprocess
import sys

result = subprocess.run([sys.argv[1]], capture_output=True, text=True, encoding="utf-8", timeout=30)
assert result.returncode == 0, result.stderr
report = json.loads(result.stdout)
assert report["unicode_services"] is True
assert len(report["fonts"]) == 6
assert report["fonts"][0]["covered"] is True
assert all(isinstance(row["covered"], bool) and row["families"] for row in report["fonts"])
assert all(isinstance(path, str) for path in report["libraries"])
print("Native diagnostic Unicode and font probe passed")
