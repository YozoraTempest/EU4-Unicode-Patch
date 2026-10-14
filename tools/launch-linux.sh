#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then
    printf '%s\n' 'Usage: launch-linux.sh GAME_COMMAND [ARGUMENT ...]' >&2
    exit 2
fi
export LD_PRELOAD="$patch_dir/libeu4_unicode.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$@"
