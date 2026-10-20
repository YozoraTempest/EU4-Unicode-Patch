#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf '%s\n' 'Usage: test-linux-runtime.sh ARTIFACT_DIRECTORY LOADER_FIXTURE OUTPUT_DIRECTORY' >&2
    exit 2
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(dirname -- "$script_dir")"
artifact_dir="$(realpath "$1")"
fixture="$(realpath "$2")"
mkdir -p "$3"
output_dir="$(realpath "$3")"
package_dir="$output_dir/package"
archive="EU4UnicodePatch-linux-x86_64-v$(cat "$project_root/VERSION").tar.gz"

# Verify and unpack the producer's archive, preserving its executable bits.
# No library or launcher is rebuilt or substituted for the consumer host.
(
    cd -- "$artifact_dir"
    sha256sum -c linux-package-sha256.txt
) | tee "$output_dir/archive-checksums.log"
mkdir "$package_dir"
tar -xzf "$artifact_dir/$archive" -C "$package_dir"
(
    cd -- "$package_dir"
    sha256sum -c SHA256SUMS
    for script in ./*.sh; do
        test -x "$script"
    done
) | tee "$output_dir/package-checksums.log"
cp /etc/os-release "$output_dir/runtime-os-release.txt"
sha256sum "$package_dir/libeu4_unicode.so" > "$output_dir/runtime-binary-sha256.txt"
ldd "$package_dir/libeu4_unicode.so" > "$output_dir/runtime-dependencies.txt"
if grep -Fq 'not found' "$output_dir/runtime-dependencies.txt"; then
    cat "$output_dir/runtime-dependencies.txt" >&2
    exit 1
fi
python3 "$project_root/tests/linux_loader_test.py" \
    "$package_dir/libeu4_unicode.so" "$fixture" "$package_dir/launch-linux.sh" \
    | tee "$output_dir/loader.log"
python3 "$project_root/tests/linux_launcher_test.py" \
    "$package_dir/libeu4_unicode.so" "$fixture" "$package_dir" \
    | tee "$output_dir/launcher.log"
printf '%s\n' 'Generic package preload and Paradox Launcher integration checks passed.'

# The independent diagnostic artifact must also start with runtime dependencies only.
diagnostic_archives=("$artifact_dir"/EU4UnicodePatch-diagnostics-linux-x86_64-*.tar.gz)
if [[ ${#diagnostic_archives[@]} -ne 1 || ! -f ${diagnostic_archives[0]} ]]; then
    printf '%s\n' 'Expected one independent Linux diagnostic archive.' >&2
    exit 1
fi
mkdir "$output_dir/diagnostics"
tar -xzf "${diagnostic_archives[0]}" -C "$output_dir/diagnostics"
python3 "$project_root/tests/diagnostic_probe_test.py" "$output_dir/diagnostics/diagnostic_probe"
python3 "$project_root/tests/diagnostics_package_test.py" --platform linux --stage "$output_dir/diagnostics" --payload-directory "$package_dir" \
    | tee "$output_dir/diagnostics.log"
