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
mkdir -p build/linux-package
cp build/libeu4_unicode.so tools/launch-linux.sh build/linux-package/
cp LICENSE build/linux-package/
mkdir -p build/linux-package/licenses
cp vendor/safetyhook/LICENSE build/linux-package/licenses/SafetyHook.txt
cp build/_deps/zydis-src/LICENSE build/linux-package/licenses/Zydis.txt
cp build/_deps/zydis-src/dependencies/zycore/LICENSE build/linux-package/licenses/Zycore.txt
chmod +x build/linux-package/launch-linux.sh
file build/libeu4_unicode.so > build/linux-package/elf.txt
readelf -d build/libeu4_unicode.so > build/linux-package/dynamic.txt
ldd build/libeu4_unicode.so > build/linux-package/dependencies.txt
LD_PRELOAD="$PWD/build/libeu4_unicode.so" /usr/bin/true
(cd build/linux-package && sha256sum libeu4_unicode.so launch-linux.sh > SHA256SUMS)
