#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then
    printf '%s\n' 'Usage: launch-linux.sh GAME_COMMAND [ARGUMENT ...]' \
        'Steam: launch-linux.sh --steam /path/to/eu4 [GAME_ARGUMENT ...] -- %command%' >&2
    exit 2
fi
if [[ ! -r "$patch_dir/libeu4_unicode.so" ]]; then
    printf '%s\n' 'libeu4_unicode.so is missing beside launch-linux.sh.' >&2
    exit 1
fi
# Arch libraries use the host ABI. Execute the native game before Steam's
# container command; retain the Steam client's App ID, IPC and runtime paths.
# The explicit game path and separator avoid guessing inside Steam wrappers.
if [[ $1 == --steam ]]; then
    shift
    if [[ $# -eq 0 || ! -x $1 || ${1##*/} != eu4 ]]; then
        printf '%s\n' 'Steam mode requires the native eu4 executable path.' >&2
        exit 2
    fi
    native_game=$1
    shift
    native_arguments=()
    while [[ $# -gt 0 && $1 != -- ]]; do
        native_arguments+=("$1")
        shift
    done
    if [[ $# -lt 2 ]]; then
        printf '%s\n' 'Steam mode requires -- followed by %command%.' >&2
        exit 2
    fi
    native_directory="$(cd -- "$(dirname -- "$native_game")" && pwd)"
    set -- "$native_directory/eu4" -skiplauncher "${native_arguments[@]}"
    cd -- "$native_directory"
fi
# The game's embedded SDL 2.0.4 lacks a working IME context in this profile.
# Its official dynamic API uses the maintained SDL2 ABI supplied by Arch.
export SDL_DYNAMIC_API="${SDL_DYNAMIC_API:-/usr/lib/libSDL2-2.0.so.0}"
# Steam can force LC_ALL=C. XIM then cannot convert committed Chinese text.
# Arch provides C.UTF-8 with the same C numeric conventions and UTF-8 ctype.
case ${LC_ALL:-} in
    C|POSIX) export LC_ALL=C.UTF-8 ;;
esac
export LD_PRELOAD="$patch_dir/libeu4_unicode.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$@"
