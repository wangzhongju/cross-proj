#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=${1:-/workspace}
MODEL=${2:-P550}
export WORKSPACE MODEL

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection

cleanup() {
  if [ "${KEEP_MOUNT:-0}" = "1" ]; then
    return
  fi
  "$WORKSPACE/scripts/riscv_env_setup.sh" "$WORKSPACE" "$MODEL" cleanup >/dev/null 2>&1 || true
}
trap cleanup EXIT

print_artifact_info() {
  local artifact=$1
  if [ ! -e "$artifact" ]; then
    echo "artifact not found: $artifact" >&2
    exit 1
  fi

  file "$artifact"
  riscv64-unknown-linux-gnu-readelf -h "$artifact" | grep -E "Machine|Flags"
}

"$WORKSPACE/scripts/riscv_env_setup.sh" "$WORKSPACE" "$MODEL" setup
if [ "${SKIP_DEPS:-0}" != "1" ]; then
  "$WORKSPACE/scripts/riscv_env_setup.sh" "$WORKSPACE" "$MODEL" deps
fi
source "$WORKSPACE/scripts/source_sdk_env.sh" "$MODEL"

case "$SDK_VERSION" in
  20250730)
    cd "$WORKSPACE/yolov5s/src"
    ./build.sh /opt/riscv/sysroot
    print_artifact_info "build/sample_npu"
    ;;
  202606)
    cd "$WORKSPACE/proj/media-agent/third_party/algorithm"
    ./build.sh --target riscv64 --sysroot /opt/riscv/sysroot -q -t
    artifact=$(find build-riscv64 -type f -name "*.so" -print -quit)
    if [ -z "$artifact" ]; then
      echo "no shared-library artifact found under build-riscv64" >&2
      exit 1
    fi
    print_artifact_info "$artifact"
    ;;
  *)
    echo "unsupported SDK_VERSION for cross-compile verification: $SDK_VERSION" >&2
    exit 1
    ;;
esac
