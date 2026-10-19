#!/usr/bin/env bash
set -euo pipefail
patch_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ $# -eq 0 ]]; then
    printf '%s\n' 'Usage: launch-linux.sh GAME_COMMAND [ARGUMENT ...]' \
        'Steam: launch-linux.sh --steam /path/to/eu4 [GAME_ARGUMENT ...] -- %command%' \
        'Paradox Launcher: use install-paradox-launcher.sh once, then leave Steam launch options empty.' >&2
    exit 2
fi
if [[ ! -r "$patch_dir/libeu4_unicode.so" ]]; then
    printf '%s\n' 'libeu4_unicode.so is missing beside launch-linux.sh.' >&2
    exit 1
fi
preload_library="$patch_dir/libeu4_unicode.so"
# Execute the native game before Steam's container command; retain the Steam
# client's App ID, IPC and runtime paths.
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
# The Paradox Launcher invokes only this game wrapper. Keep its arguments
# unchanged and use a relative preload path: LD_PRELOAD cannot quote the
# spaces in Steam's usual "Europa Universalis IV" installation directory.
if [[ $1 == --paradox ]]; then
    shift
    if [[ ${patch_dir##*/} != .eu4-unicode || ! -x "$patch_dir/../eu4" ]]; then
        printf '%s\n' 'Paradox mode requires an installed .eu4-unicode directory beside eu4.' >&2
        exit 2
    fi
    cd -- "$patch_dir/.."
    preload_library=./.eu4-unicode/libeu4_unicode.so
    set -- ./eu4 "$@"
fi
# The game's embedded SDL 2.0.4 lacks a working IME context in this profile.
# Respect an explicit runtime choice first, then discover the conventional
# SDL2 ABI path used by Ubuntu, Arch and Steam Runtime installations.
if [[ -z ${SDL_DYNAMIC_API:-} ]]; then
    for candidate in \
        "$patch_dir/lib/libSDL2-2.0.so.0" \
        /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 \
        /usr/lib64/libSDL2-2.0.so.0 \
        /usr/lib/libSDL2-2.0.so.0; do
        if [[ -r $candidate ]]; then
            export SDL_DYNAMIC_API="$candidate"
            break
        fi
    done
fi
# Steam can force LC_ALL=C. XIM then cannot convert committed Chinese text.
# glibc distributions provide C.UTF-8 with the same C numeric conventions and
# UTF-8 ctype.
case ${LC_ALL:-} in
    C|POSIX) export LC_ALL=C.UTF-8 ;;
esac
export LD_PRELOAD="$preload_library${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$@"
