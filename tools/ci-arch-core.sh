#!/usr/bin/env bash
set -euo pipefail
if ! grep -q '^ID=arch$' /etc/os-release; then
    printf '%s\n' 'This compatibility entry point must run on Arch Linux.' >&2
    exit 1
fi
exec "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/ci-linux-core.sh" "$@"
