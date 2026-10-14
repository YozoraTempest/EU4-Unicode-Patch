#!/usr/bin/env bash
set -euo pipefail
if ! grep -q '^ID=arch$' /etc/os-release; then
    printf '%s\n' 'This job must run on Arch Linux.' >&2
    exit 1
fi
mkdir -p build
cp /etc/os-release build/arch-os-release.txt
uname -a > build/arch-kernel.txt
pacman -Q > build/arch-packages.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --output-junit ctest-arch-results.xml
