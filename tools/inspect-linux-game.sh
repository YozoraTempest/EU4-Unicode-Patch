#!/usr/bin/env bash
# Read-only native executable evidence; this script does not launch the game.
set -euo pipefail
export LC_ALL=C
if [[ $# -ne 2 ]]; then
    printf '%s\n' 'Usage: inspect-linux-game.sh /path/to/eu4 /new/evidence/directory' >&2
    exit 2
fi
game=$(realpath -- "$1")
evidence=$2
[[ -f "$game" ]] || { printf '%s\n' 'Game executable does not exist.' >&2; exit 1; }
[[ ! -e "$evidence" ]] || { printf '%s\n' 'Evidence directory must be new.' >&2; exit 1; }
description=$(file -b -- "$game")
if [[ "$description" != *'ELF 64-bit'* || "$description" != *'x86-64'* ]]; then
    printf 'Expected the Steam Linux x86_64 ELF executable, got: %s\n' "$description" >&2
    exit 1
fi
mkdir -p -- "$evidence"
printf '%s\n' "$game" > "$evidence/executable-path.txt"
printf '%s\n' "$description" > "$evidence/executable-format.txt"
sha256sum -- "$game" > "$evidence/executable-sha256.txt"
readelf --wide --file-header -- "$game" > "$evidence/elf-header.txt"
readelf --wide --program-headers -- "$game" > "$evidence/elf-program-headers.txt"
readelf --wide --dynamic -- "$game" > "$evidence/elf-dynamic.txt"
readelf --wide --notes -- "$game" > "$evidence/elf-notes.txt"
readelf --wide --symbols -- "$game" > "$evidence/elf-symbols.txt"
objdump --file-headers -- "$game" > "$evidence/objdump-header.txt"
cp /etc/os-release "$evidence/os-release.txt"
uname -a > "$evidence/kernel.txt"
if command -v pacman >/dev/null; then pacman -Q > "$evidence/packages.txt"; fi
for name in eu4_branch.txt eu4_rev.txt; do
    path="$(dirname -- "$game")/$name"
    if [[ -f "$path" ]]; then cp -- "$path" "$evidence/$name"; fi
done
printf 'Native Linux executable evidence saved to %s\n' "$evidence"
