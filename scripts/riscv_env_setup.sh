#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=${1:-/workspace}
MODEL=${2:-P550}
ACTION=${3:-setup}
shift 3 || true
export WORKSPACE
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection

case "$ACTION" in
  setup)
    "$WORKSPACE/scripts/chroot_mount.sh" mount "$MODEL"
    "$WORKSPACE/scripts/use_rootfs_sysroot.sh" link "$MODEL"
    echo "rootfs sysroot is ready. In the current shell run:"
    echo "  SDK_VERSION=$SDK_VERSION source $WORKSPACE/scripts/source_sdk_env.sh $MODEL"
    ;;
  mount) "$WORKSPACE/scripts/chroot_mount.sh" mount "$MODEL" "$@" ;;
  umount|unmount) "$WORKSPACE/scripts/chroot_mount.sh" umount "$MODEL" ;;
  status) "$WORKSPACE/scripts/chroot_mount.sh" status "$MODEL" ;;
  deps) "$WORKSPACE/scripts/install_chroot_deps.sh" "$MODEL" ;;
  exec) "$WORKSPACE/scripts/chroot_exec.sh" "$MODEL" "$@" ;;
  sysroot-link) "$WORKSPACE/scripts/use_rootfs_sysroot.sh" link "$MODEL" ;;
  sysroot-restore) "$WORKSPACE/scripts/use_rootfs_sysroot.sh" restore "$MODEL" ;;
  cleanup)
    "$WORKSPACE/scripts/use_rootfs_sysroot.sh" restore "$MODEL" || true
    "$WORKSPACE/scripts/chroot_mount.sh" umount "$MODEL"
    ;;
  *)
    echo "Usage: $0 [WORKSPACE] [MODEL] {setup|mount|umount|status|deps|exec|sysroot-link|sysroot-restore|cleanup} [args]" >&2
    exit 1
    ;;
esac
