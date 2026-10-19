#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$patch_dir/linux-launcher.py" install "$@"
