#!/usr/bin/env bash
set -euo pipefail

ACTION=${1:-status}
MODEL=${2:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SDK_DIR=${SDK_DIR:-$WORKSPACE/eswin-sdk-20250730}
OUTPUT_DIR=${OUTPUT_DIR:-$SDK_DIR/$MODEL/output}
ROOTFS_DIR=${ROOTFS_DIR:-$OUTPUT_DIR/rootfs}
PROJECT_MOUNT=${PROJECT_MOUNT:-$ROOTFS_DIR/mnt/workspace}
ROOTFS_MIN_SIZE=${ROOTFS_MIN_SIZE:-10G}
BOOT_MIN_SIZE=${BOOT_MIN_SIZE:-500M}

latest_image() {
  local prefix=$1
  if [ ! -d "$OUTPUT_DIR" ]; then
    return 0
  fi
  find "$OUTPUT_DIR" -maxdepth 1 -type f -name "${prefix}-${MODEL}-*.ext4" | sort | tail -n 1
}

ROOT_IMAGE=${ROOT_IMAGE:-${3:-$(latest_image root)}}
BOOT_IMAGE=${BOOT_IMAGE:-${4:-$(latest_image boot)}}

size_to_bytes() { numfmt --from=iec "$1"; }

ensure_ext4_min_size() {
  local image=$1 min_size=$2 label=$3
  [ -n "$image" ] || return 0
  [ -f "$image" ] || return 0
  if findmnt -rn --source "$image" >/dev/null 2>&1; then
    echo "skip resize mounted ${label}: $image"
    return 0
  fi
  local current min_bytes
  current=$(stat -c %s "$image")
  min_bytes=$(size_to_bytes "$min_size")
  if [ "$current" -ge "$min_bytes" ]; then
    echo "${label} image size is ok: $image ($(numfmt --to=iec "$current"))"
    return 0
  fi
  echo "resize ${label} image before mount: $image -> $min_size"
  sudo truncate -s "$min_size" "$image"
  sudo e2fsck -fy "$image" >/dev/null
  sudo resize2fs "$image" >/dev/null
}

mount_one() {
  local src=$1 dst=$2
  if mountpoint -q "$dst"; then
    echo "already mounted: $dst"
  else
    sudo mkdir -p "$dst"
    sudo mount "$src" "$dst"
  fi
}

bind_one() {
  local src=$1 dst=$2
  if mountpoint -q "$dst"; then
    echo "already bind-mounted: $dst"
  else
    sudo mkdir -p "$dst"
    sudo mount --bind "$src" "$dst"
  fi
}

mount_all() {
  [ -n "$ROOT_IMAGE" ] || { echo "root image not found under $OUTPUT_DIR" >&2; exit 1; }
  [ -n "$BOOT_IMAGE" ] || { echo "boot image not found under $OUTPUT_DIR" >&2; exit 1; }
  echo "root image: $ROOT_IMAGE"
  echo "boot image: $BOOT_IMAGE"
  ensure_ext4_min_size "$ROOT_IMAGE" "$ROOTFS_MIN_SIZE" rootfs
  ensure_ext4_min_size "$BOOT_IMAGE" "$BOOT_MIN_SIZE" boot
  mount_one "$ROOT_IMAGE" "$ROOTFS_DIR"
  mount_one "$BOOT_IMAGE" "$ROOTFS_DIR/boot"
  for d in dev proc sys run tmp; do bind_one "/$d" "$ROOTFS_DIR/$d"; done
  if [ -d /dev/pts ]; then bind_one /dev/pts "$ROOTFS_DIR/dev/pts"; fi
  bind_one "$WORKSPACE" "$PROJECT_MOUNT"
  sudo touch "$ROOTFS_DIR/.cross_proj_mounted"
  echo "mounted rootfs: $ROOTFS_DIR"
}

umount_all() {
  local targets=("$PROJECT_MOUNT" "$ROOTFS_DIR/dev/pts" "$ROOTFS_DIR/tmp" "$ROOTFS_DIR/run" "$ROOTFS_DIR/sys" "$ROOTFS_DIR/proc" "$ROOTFS_DIR/dev" "$ROOTFS_DIR/boot" "$ROOTFS_DIR")
  for d in "${targets[@]}"; do
    if mountpoint -q "$d"; then
      sudo umount -lf "$d"
      echo "umounted: $d"
    fi
  done
}

status() {
  echo "output dir: $OUTPUT_DIR"
  echo "root image: ${ROOT_IMAGE:-not found}"
  if [ -f "${ROOT_IMAGE:-}" ]; then stat -c 'root size: %s bytes' "$ROOT_IMAGE"; fi
  echo "boot image: ${BOOT_IMAGE:-not found}"
  if [ -f "${BOOT_IMAGE:-}" ]; then stat -c 'boot size: %s bytes' "$BOOT_IMAGE"; fi
  mount | grep -E "$ROOTFS_DIR|$PROJECT_MOUNT" || true
}

case "$ACTION" in
  mount) mount_all ;;
  umount|unmount) umount_all ;;
  status) status ;;
  *) echo "Usage: $0 {mount|umount|status} [MODEL] [ROOT_IMAGE] [BOOT_IMAGE]" >&2; exit 1 ;;
esac
