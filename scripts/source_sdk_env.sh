#!/usr/bin/env bash
# Source this file from an interactive shell:
#   source /workspace/scripts/source_sdk_env.sh P550

MODEL=${1:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SDK_DIR=${SDK_DIR:-$WORKSPACE/eswin-sdk-20250730}

case "$MODEL" in
    EIC7700FG-LP4*|EIC7700FG-LP5*) choice=1 ;;
    EIC7700FG-EVB-LP5-A2) choice=2 ;;
    EIC7700-E-L5G4) choice=3 ;;
    EIC7702-E-L5G5) choice=4 ;;
    P550) choice=5 ;;
    EIC7700-02-1154B1) choice=6 ;;
    EBC7702-D01) choice=7 ;;
    *) echo "unsupported model: $MODEL" >&2; return 1 2>/dev/null || exit 1 ;;
esac

if [ ! -f "$SDK_DIR/setenv.sh" ]; then
    echo "SDK setenv not found: $SDK_DIR/setenv.sh" >&2
    return 1 2>/dev/null || exit 1
fi

cd "$SDK_DIR"
source "$SDK_DIR/setenv.sh" <<< "$choice"
cd "$WORKSPACE"
