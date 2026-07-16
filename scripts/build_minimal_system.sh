#!/usr/bin/env bash
set -eo pipefail

MODEL=${1:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
LOG_DIR=${LOG_DIR:-$WORKSPACE/logs}
mkdir -p "$LOG_DIR"

if [ ! -f "$SDK_DIR/setenv.sh" ]; then
    echo "SDK setenv not found: $SDK_DIR/setenv.sh" >&2
    exit 1
fi

if [ -x "$WORKSPACE/scripts/patch_sdk_sources.sh" ]; then
    "$WORKSPACE/scripts/patch_sdk_sources.sh"
fi
source "$WORKSPACE/scripts/source_sdk_env.sh" "$MODEL"
if [ -x "$WORKSPACE/scripts/patch_sdk_sources.sh" ]; then
    "$WORKSPACE/scripts/patch_sdk_sources.sh"
fi
cd "$SDK_DIR"

log_file="$LOG_DIR/build-${MODEL}-$(date +%Y%m%d-%H%M%S).log"
echo "building SDK $SDK_VERSION ($SDK_LAYOUT) for $MODEL -> $SDK_BOARD_NAME"
echo "log: $log_file"
# The SDK functions read optional positional parameters directly, so nounset must be off here.
set +u
make_all 2>&1 | tee "$log_file"
