#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -lt 3 || $2 != -- ]]; then
    printf '%s\n' 'Steam: start-paradox-launcher.sh /path/to/game-directory -- %command%' >&2
    exit 2
fi
game_dir=$1
shift 2
"$patch_dir/install-paradox-launcher.sh" "$game_dir"
# Resume Steam's original command, including dowser and the Paradox Launcher.
# Preloading begins only when the launcher invokes the installed game wrapper.
exec "$@"
