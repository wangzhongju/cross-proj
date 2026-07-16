#!/usr/bin/env bash
# Source this file from an interactive shell:
#   source /workspace/scripts/source_sdk_env.sh P550

MODEL=${1:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
choice=$(sdk_source_choice "$MODEL" "$SDK_DIR") || return 1 2>/dev/null || exit 1

if [ ! -f "$SDK_DIR/setenv.sh" ]; then
    echo "SDK setenv not found: $SDK_DIR/setenv.sh" >&2
    return 1 2>/dev/null || exit 1
fi

cd "$SDK_DIR"
source "$SDK_DIR/setenv.sh" <<< "$choice"
SDK_BOARD_NAME=${board_name:-$SDK_BOARD_NAME}
SDK_OUTPUT_DIR=${SDK_DIR}/${SDK_BOARD_NAME}/output
export SDK_BOARD_NAME SDK_OUTPUT_DIR
cd "$WORKSPACE"
