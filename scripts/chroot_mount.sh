#!/usr/bin/env bash
set -euo pipefail

ACTION=${1:-status}
MODEL=${2:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
OUTPUT_DIR=${OUTPUT_DIR:-$SDK_OUTPUT_DIR}
ROOTFS_DIR=${ROOTFS_DIR:-$OUTPUT_DIR/rootfs}
PROJECT_MOUNT=${PROJECT_MOUNT:-$ROOTFS_DIR/mnt/workspace}
ROOTFS_MIN_SIZE=${ROOTFS_MIN_SIZE:-10G}
BOOT_MIN_SIZE=${BOOT_MIN_SIZE:-500M}
LOOP_MARKER=${LOOP_MARKER:-$OUTPUT_DIR/.cross_proj_loop}

latest_image() {
  local prefix=$1
  if [ ! -d "$OUTPUT_DIR" ]; then
    return 0
  fi
  find "$OUTPUT_DIR" -maxdepth 1 -type f -name "${prefix}-${MODEL}-*.ext4" | sort | tail -n 1
}

latest_disk_image() {
  if [ ! -d "$OUTPUT_DIR" ]; then
    return 0
  fi
  find "$OUTPUT_DIR" -maxdepth 1 -type f -name "*.img" | sort | tail -n 1
}

ROOT_IMAGE=${ROOT_IMAGE:-${3:-$(latest_image root)}}
BOOT_IMAGE=${BOOT_IMAGE:-${4:-$(latest_image boot)}}
DISK_IMAGE=${DISK_IMAGE:-$(latest_disk_image)}

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

bind_file() {
  local src=$1 dst=$2
  if mountpoint -q "$dst"; then
    echo "already bind-mounted: $dst"
  else
    sudo mkdir -p "$(dirname "$dst")"
    sudo touch "$dst"
    sudo mount --bind "$src" "$dst"
  fi
}

rootfs_resolv_target() {
  local resolv="$ROOTFS_DIR/etc/resolv.conf"
  if [ -L "$resolv" ]; then
    local link
    link=$(readlink "$resolv")
    if [[ "$link" = /* ]]; then
      realpath -m "$ROOTFS_DIR/$link"
    else
      realpath -m "$(dirname "$resolv")/$link"
    fi
  else
    echo "$resolv"
  fi
}

bind_resolv_conf() {
  local target
  target=$(rootfs_resolv_target)
  bind_file /etc/resolv.conf "$target"
}

mounted_loop() {
  if [ -f "$LOOP_MARKER" ]; then
    local loopdev
    loopdev=$(cat "$LOOP_MARKER")
    if [ -n "$loopdev" ] && losetup "$loopdev" >/dev/null 2>&1; then
      echo "$loopdev"
    fi
  fi
}

mount_disk_image() {
  [ -n "$DISK_IMAGE" ] || { echo "disk image not found under $OUTPUT_DIR" >&2; exit 1; }
  [ -f "$DISK_IMAGE" ] || { echo "disk image not found: $DISK_IMAGE" >&2; exit 1; }
  echo "disk image: $DISK_IMAGE"
  local loopdev root_part boot_part
  loopdev=$(mounted_loop || true)
  if [ -z "$loopdev" ]; then
    loopdev=$(sudo losetup --find --show -P "$DISK_IMAGE")
    mkdir -p "$OUTPUT_DIR"
    echo "$loopdev" > "$LOOP_MARKER"
    sleep 1
  fi
  root_part=$(lsblk -nrpo NAME,FSTYPE "$loopdev" | awk '$2=="ext4"{print $1}' | tail -n 1)
  boot_part=$(lsblk -nrpo NAME,FSTYPE "$loopdev" | awk '$2=="vfat" || $2=="fat32"{print $1; exit}')
  if [ -z "$root_part" ] && [ -b "${loopdev}p3" ]; then
    root_part="${loopdev}p3"
  fi
  if [ -z "$boot_part" ] && [ -b "${loopdev}p1" ]; then
    boot_part="${loopdev}p1"
  fi
  [ -n "$root_part" ] || { echo "ext4 root partition not found in $loopdev" >&2; exit 1; }
  mount_one "$root_part" "$ROOTFS_DIR"
  if [ -n "$boot_part" ]; then
    mount_one "$boot_part" "$ROOTFS_DIR/boot/efi"
  fi
}

mount_all() {
  if [ -n "$ROOT_IMAGE" ]; then
    [ -n "$BOOT_IMAGE" ] || { echo "boot image not found under $OUTPUT_DIR" >&2; exit 1; }
    echo "root image: $ROOT_IMAGE"
    echo "boot image: $BOOT_IMAGE"
    ensure_ext4_min_size "$ROOT_IMAGE" "$ROOTFS_MIN_SIZE" rootfs
    ensure_ext4_min_size "$BOOT_IMAGE" "$BOOT_MIN_SIZE" boot
    mount_one "$ROOT_IMAGE" "$ROOTFS_DIR"
    mount_one "$BOOT_IMAGE" "$ROOTFS_DIR/boot"
  else
    mount_disk_image
  fi
  for d in dev proc sys run tmp; do bind_one "/$d" "$ROOTFS_DIR/$d"; done
  if [ -d /dev/pts ]; then bind_one /dev/pts "$ROOTFS_DIR/dev/pts"; fi
  bind_resolv_conf
  bind_one "$WORKSPACE" "$PROJECT_MOUNT"
  sudo touch "$ROOTFS_DIR/.cross_proj_mounted"
  echo "mounted rootfs: $ROOTFS_DIR"
}

umount_all() {
  local resolv_mount
  resolv_mount=$(rootfs_resolv_target)
  local targets=("$PROJECT_MOUNT" "$ROOTFS_DIR/dev/pts" "$ROOTFS_DIR/tmp" "$resolv_mount" "$ROOTFS_DIR/run" "$ROOTFS_DIR/sys" "$ROOTFS_DIR/proc" "$ROOTFS_DIR/dev" "$ROOTFS_DIR/boot/efi" "$ROOTFS_DIR/boot" "$ROOTFS_DIR")
  for d in "${targets[@]}"; do
    if mountpoint -q "$d"; then
      sudo umount -lf "$d"
      echo "umounted: $d"
    fi
  done
  local loopdev
  loopdev=$(mounted_loop || true)
  if [ -n "$loopdev" ]; then
    sudo losetup -d "$loopdev" || true
    rm -f "$LOOP_MARKER"
    echo "detached: $loopdev"
  fi
}

status() {
  echo "sdk: $SDK_DIR ($SDK_VERSION, $SDK_LAYOUT)"
  echo "board dir: $SDK_BOARD_NAME"
  echo "output dir: $OUTPUT_DIR"
  echo "root image: ${ROOT_IMAGE:-not found}"
  if [ -f "${ROOT_IMAGE:-}" ]; then stat -c 'root size: %s bytes' "$ROOT_IMAGE"; fi
  echo "boot image: ${BOOT_IMAGE:-not found}"
  if [ -f "${BOOT_IMAGE:-}" ]; then stat -c 'boot size: %s bytes' "$BOOT_IMAGE"; fi
  echo "disk image: ${DISK_IMAGE:-not found}"
  if [ -f "${DISK_IMAGE:-}" ]; then stat -c 'disk size: %s bytes' "$DISK_IMAGE"; fi
  mount | grep -E "$ROOTFS_DIR|$PROJECT_MOUNT" || true
}

case "$ACTION" in
  mount) mount_all ;;
  umount|unmount) umount_all ;;
  status) status ;;
  *) echo "Usage: $0 {mount|umount|status} [MODEL] [ROOT_IMAGE] [BOOT_IMAGE]" >&2; exit 1 ;;
esac
