#!/usr/bin/env bash
set -euo pipefail

ACTION=${1:-status}
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
MAX_LOOP=${MAX_LOOP:-127}
PROJECT_PATTERNS=${PROJECT_PATTERNS:-/workspace/eswin-sdk-[^[:space:]]+|/home/cdky/workspace/github/cross-proj}

if [ "$(id -u)" -ne 0 ]; then
  exec sudo -E bash "$0" "$@"
fi

active_build_pids() {
  ps -eo pid=,cmd= | awk -v sdk="$SDK_DIR" '
    $0 !~ /awk -v sdk/ &&
    $0 !~ /prepare_desktop_build_env/ &&
    $0 ~ /(make_desktop_images|mkrootfs|mmdebstrap|qemu-riscv64|dpkg-buildpackage)/ &&
    $0 ~ sdk {print $1}
  '
}

ensure_binfmt() {
  mkdir -p /proc/sys/fs/binfmt_misc
  if ! mountpoint -q /proc/sys/fs/binfmt_misc; then
    mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc
  fi

  if [ ! -e /proc/sys/fs/binfmt_misc/qemu-riscv64 ]; then
    if [ ! -f /usr/lib/binfmt.d/qemu-riscv64.conf ]; then
      echo "missing /usr/lib/binfmt.d/qemu-riscv64.conf; install qemu-user-static in the container" >&2
      exit 1
    fi
    cat /usr/lib/binfmt.d/qemu-riscv64.conf > /proc/sys/fs/binfmt_misc/register
  fi

  if grep -q '^enabled$' /proc/sys/fs/binfmt_misc/qemu-riscv64; then
    echo "binfmt qemu-riscv64: enabled"
  else
    echo "binfmt qemu-riscv64 exists but is not enabled" >&2
    cat /proc/sys/fs/binfmt_misc/qemu-riscv64 >&2 || true
    exit 1
  fi
}

ensure_loop_nodes() {
  modprobe loop 2>/dev/null || true
  for i in $(seq 0 "$MAX_LOOP"); do
    if [ ! -e "/dev/loop$i" ]; then
      mknod -m 660 "/dev/loop$i" b 7 "$i"
      chgrp disk "/dev/loop$i" 2>/dev/null || true
    fi
  done
  [ -e /dev/loop-control ] || mknod -m 660 /dev/loop-control c 10 237
  chgrp disk /dev/loop-control 2>/dev/null || true
  echo "loop device nodes ready: /dev/loop0..$MAX_LOOP"
}

project_mount_targets() {
  {
    findmnt -rn -o TARGET 2>/dev/null || true
    mount | awk '{print $3}' || true
  } | grep -E "$PROJECT_PATTERNS" | grep -E '/mkimg-eswin/rootfs(/|$)' | sort -ru | sort -r || true
}

project_loop_devices() {
  losetup -a | grep -E "$PROJECT_PATTERNS" | sed -n 's#^\(/dev/loop[0-9]\+\):.*#\1#p' | sort -u || true
}

cleanup_project_leftovers() {
  local pids
  pids="$(active_build_pids | xargs echo || true)"
  if [ -n "$pids" ] && [ "${FORCE:-0}" != "1" ]; then
    echo "active SDK build process exists, refuse to cleanup: $pids" >&2
    echo "set FORCE=1 only after confirming the build is dead" >&2
    exit 1
  fi

  local target loopdev round
  for round in $(seq 1 10); do
    local found=0
    while read -r target; do
      [ -n "$target" ] || continue
      found=1
      umount -lf "$target" || true
      echo "umounted: $target"
    done < <(project_mount_targets)
    [ "$found" -eq 0 ] && break
    sleep 0.2
  done

  for round in $(seq 1 10); do
    local found=0
    while read -r loopdev; do
      [ -n "$loopdev" ] || continue
      found=1
      losetup -d "$loopdev" || true
      echo "detached: $loopdev"
    done < <(project_loop_devices)
    [ "$found" -eq 0 ] && break
    sleep 0.2
  done

  find "$SDK_DIR" -path '*/mkimg-eswin/rootfs' -type d 2>/dev/null | while read -r rootfs; do
    if mountpoint -q "$rootfs"; then
      continue
    fi
    if [ -z "$(ls -A "$rootfs" 2>/dev/null)" ]; then
      rmdir "$rootfs" && echo "removed empty stale dir: $rootfs"
    else
      local stale="${rootfs}.stale-$(date +%Y%m%d-%H%M%S)"
      mv "$rootfs" "$stale"
      echo "moved non-empty stale dir: $rootfs -> $stale"
    fi
  done
}

show_status() {
  echo "binfmt mount:"
  mount | grep binfmt_misc || true
  echo
  echo "qemu-riscv64:"
  cat /proc/sys/fs/binfmt_misc/qemu-riscv64 2>/dev/null || echo "not registered"
  echo
  echo "project loop devices:"
  losetup -a | grep -E "$PROJECT_PATTERNS" || true
  echo
  echo "project mounts:"
  project_mount_targets
  echo
  echo "active build pids:"
  active_build_pids || true
}

case "$ACTION" in
  setup)
    ensure_binfmt
    ensure_loop_nodes
    ;;
  cleanup)
    cleanup_project_leftovers
    ;;
  recover)
    cleanup_project_leftovers
    ensure_binfmt
    ensure_loop_nodes
    ;;
  status)
    show_status
    ;;
  *)
    echo "Usage: $0 {setup|cleanup|recover|status}" >&2
    exit 1
    ;;
esac
