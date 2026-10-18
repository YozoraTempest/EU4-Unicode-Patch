#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/.." && pwd)"
build_dir="${BUILD_DIRECTORY:-build}"
ctest_exclude="${CTEST_EXCLUDE_REGEX:-}"

if [[ "$(uname -m)" != x86_64 ]]; then
    printf '%s\n' 'The generic Linux package currently targets x86_64.' >&2
    exit 1
fi

cd -- "$project_root"
mkdir -p "$build_dir"
if [[ -r /etc/os-release ]]; then
    cp /etc/os-release "$build_dir/linux-os-release.txt"
fi
uname -a > "$build_dir/linux-kernel.txt"
if command -v pacman >/dev/null 2>&1; then
    pacman -Q > "$build_dir/linux-packages.txt"
    if grep -q '^ID=arch$' /etc/os-release; then
        cp /etc/os-release "$build_dir/arch-os-release.txt"
        cp "$build_dir/linux-kernel.txt" "$build_dir/arch-kernel.txt"
        cp "$build_dir/linux-packages.txt" "$build_dir/arch-packages.txt"
    fi
elif command -v dpkg-query >/dev/null 2>&1; then
    dpkg-query -W -f='${binary:Package}\t${Version}\n' > "$build_dir/linux-packages.txt"
else
    printf '%s\n' 'Cannot record the host package set.' >&2
    exit 1
fi

cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel 2

ctest_args=(--test-dir "$build_dir" --output-on-failure --output-junit ctest-linux-results.xml)
if [[ -n "$ctest_exclude" ]]; then
    ctest_args+=(-E "$ctest_exclude")
fi
ctest "${ctest_args[@]}"

package_dir="$build_dir/linux-package"
mkdir -p "$package_dir/licenses"
cp "$build_dir/libeu4_unicode.so" tools/launch-linux.sh LICENSE "$package_dir/"
cp vendor/safetyhook/LICENSE "$package_dir/licenses/SafetyHook.txt"
cp vendor/picosha2/LICENSE "$package_dir/licenses/PicoSHA2.txt"
cp "$build_dir/_deps/zydis-src/LICENSE" "$package_dir/licenses/Zydis.txt"
cp "$build_dir/_deps/zydis-src/dependencies/zycore/LICENSE" "$package_dir/licenses/Zycore.txt"
chmod +x "$package_dir/launch-linux.sh"
file "$build_dir/libeu4_unicode.so" > "$package_dir/elf.txt"
readelf -d "$build_dir/libeu4_unicode.so" > "$package_dir/dynamic.txt"
ldd "$build_dir/libeu4_unicode.so" > "$package_dir/dependencies.txt"
if grep -Eq 'libicu(uc|i18n|data)\.so' "$package_dir/dynamic.txt"; then
    printf '%s\n' 'The generic Linux plugin must not have a link-time ICU SONAME.' >&2
    exit 1
fi
LD_PRELOAD="$(realpath "$build_dir/libeu4_unicode.so")" /usr/bin/true
(
    cd "$package_dir"
    sha256sum libeu4_unicode.so launch-linux.sh > SHA256SUMS
)

linux_archive="EU4UnicodePatch-linux-x86_64-v$(cat VERSION).tar.gz"
tar -C "$package_dir" -czf "$build_dir/$linux_archive" .
(
    cd "$build_dir"
    sha256sum "$linux_archive" > linux-package-sha256.txt
)
