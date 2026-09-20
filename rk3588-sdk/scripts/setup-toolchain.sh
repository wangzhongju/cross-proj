#!/usr/bin/env bash
set -euo pipefail

sdk_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain="${sdk_root}/prebuilts/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu"
tool_bin="${toolchain}/bin"
compat="${sdk_root}/compat-bin"

if [[ ! -x "${tool_bin}/aarch64-none-linux-gnu-gcc" ]]; then
    echo "Missing Rockchip SDK GCC: ${tool_bin}/aarch64-none-linux-gnu-gcc" >&2
    exit 1
fi

mkdir -p "${compat}"
for name in gcc g++ cpp ar as ld nm objcopy objdump ranlib readelf size strings strip; do
    if [[ -x "${tool_bin}/aarch64-none-linux-gnu-${name}" ]]; then
        ln -sfn "../prebuilts/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-${name}" \
            "${compat}/aarch64-linux-gnu-${name}"
    fi
done

"${tool_bin}/aarch64-none-linux-gnu-gcc" --version | head -1
"${tool_bin}/aarch64-none-linux-gnu-gcc" -dumpmachine
git -C "${toolchain}" rev-parse HEAD 2>/dev/null || true
