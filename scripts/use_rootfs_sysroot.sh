#!/usr/bin/env bash
set -euo pipefail

ACTION=${1:-link}
MODEL=${2:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/sdk_common.sh"
sdk_export_selection
ROOTFS_DIR=${ROOTFS_DIR:-$SDK_OUTPUT_DIR/rootfs}
SYSROOT=/opt/riscv/sysroot
BACKUP=/opt/riscv/sysroot.toolchain

link_sysroot() {
    if [ ! -d /opt/riscv ]; then
        echo "/opt/riscv not found" >&2
        exit 1
    fi
    if [ ! -d "$ROOTFS_DIR/usr" ]; then
        echo "rootfs not prepared: $ROOTFS_DIR" >&2
        exit 1
    fi
    if [ -e "$SYSROOT" ] && [ ! -L "$SYSROOT" ] && [ ! -e "$BACKUP" ]; then
        sudo mv "$SYSROOT" "$BACKUP"
    fi
    sudo ln -sfn "$ROOTFS_DIR" "$SYSROOT"
    echo "linked $SYSROOT -> $ROOTFS_DIR"
}

restore_sysroot() {
    if [ -L "$SYSROOT" ]; then
        sudo rm -f "$SYSROOT"
    fi
    if [ -e "$BACKUP" ] && [ ! -e "$SYSROOT" ]; then
        sudo mv "$BACKUP" "$SYSROOT"
        echo "restored $SYSROOT from $BACKUP"
    fi
}

case "$ACTION" in
    link) link_sysroot ;;
    restore) restore_sysroot ;;
    *) echo "Usage: $0 {link|restore} [MODEL]" >&2; exit 1 ;;
esac
