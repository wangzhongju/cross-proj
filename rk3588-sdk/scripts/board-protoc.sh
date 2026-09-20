#!/usr/bin/env bash
set -euo pipefail

sdk_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target_protoc="${sdk_root}/sysroot/usr/bin/protoc"
[[ -x "${target_protoc}" ]] || {
    echo "Missing target protoc. Mount/prepare the RK3588 rootfs as documented in /workspace/run.md." >&2
    exit 1
}

exec qemu-aarch64-static -L "${sdk_root}/sysroot" "${target_protoc}" "$@"
