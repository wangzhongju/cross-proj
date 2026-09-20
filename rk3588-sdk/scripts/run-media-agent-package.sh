#!/usr/bin/env bash
set -euo pipefail

sdk_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace="$(cd "${sdk_root}/.." && pwd)"
jobs="${1:-8}"
[[ "${jobs}" =~ ^[1-9][0-9]*$ ]] || { echo "jobs must be positive" >&2; exit 2; }

mkdir -p "${sdk_root}/artifacts"
log="${sdk_root}/artifacts/media-agent-package-aarch64.log"
cd "${workspace}/proj/media-agent"
echo "Running: ./scripts/package_deb.sh --target aarch64 --jobs ${jobs} --install"
echo "Log: ${log}"
if ./scripts/package_deb.sh --target aarch64 --jobs "${jobs}" --install >"${log}" 2>&1; then
    tail -n 35 "${log}"
else
    result=$?
    tail -n 100 "${log}" >&2
    exit "${result}"
fi
