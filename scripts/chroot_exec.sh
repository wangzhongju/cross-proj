#!/usr/bin/env bash
set -euo pipefail

MODEL=${1:-P550}
shift || true
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
ROOTFS_DIR=${ROOTFS_DIR:-$SDK_OUTPUT_DIR/rootfs}

if ! mountpoint -q "$ROOTFS_DIR"; then
    echo "rootfs is not mounted: $ROOTFS_DIR" >&2
    echo "run: /workspace/scripts/chroot_mount.sh mount $MODEL" >&2
    exit 1
fi

if [ $# -gt 0 ]; then
    sudo chroot "$ROOTFS_DIR" /bin/bash -lc "$*"
elif [ -t 0 ]; then
    sudo chroot "$ROOTFS_DIR" /bin/bash
else
    echo "rootfs mounted: $ROOTFS_DIR"
    echo "enter with: $0 $MODEL"
fi
