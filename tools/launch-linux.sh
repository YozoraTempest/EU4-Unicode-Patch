#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then
    printf '%s\n' 'Usage: launch-linux.sh GAME_COMMAND [ARGUMENT ...]' >&2
    exit 2
fi
if [[ ! -r "$patch_dir/libeu4_unicode.so" ]]; then
    printf '%s\n' 'libeu4_unicode.so is missing beside launch-linux.sh.' >&2
    exit 1
fi
# The game's embedded SDL 2.0.4 lacks a working IME context in this profile.
# Its official dynamic API uses the maintained SDL2 ABI supplied by Arch.
export SDL_DYNAMIC_API="${SDL_DYNAMIC_API:-/usr/lib/libSDL2-2.0.so.0}"
export LD_PRELOAD="$patch_dir/libeu4_unicode.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$@"
