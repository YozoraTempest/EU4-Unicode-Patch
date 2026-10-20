#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    printf '%s\n' 'Python 3 is required. Install python3 using your distribution package manager.' >&2
    exit 2
fi
exec python3 "$script_dir/diagnose-linux.py" "$@"
