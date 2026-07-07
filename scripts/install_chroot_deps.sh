#!/usr/bin/env bash
set -euo pipefail

MODEL=${1:-P550}
WORKSPACE=${WORKSPACE:-/workspace}
SDK_DIR=${SDK_DIR:-$WORKSPACE/eswin-sdk-20250730}
ROOTFS_DIR=${ROOTFS_DIR:-$SDK_DIR/$MODEL/output/rootfs}
KERNEL_UAPI=$SDK_DIR/source/linux-eswin/include/uapi/linux

if ! mountpoint -q "$ROOTFS_DIR"; then
    echo "rootfs is not mounted: $ROOTFS_DIR" >&2
    echo "run: /workspace/scripts/chroot_mount.sh mount $MODEL" >&2
    exit 1
fi

if [ -d "$KERNEL_UAPI" ]; then
    sudo cp -an "$KERNEL_UAPI"/*.h "$ROOTFS_DIR/usr/include/linux/" || true
fi

sudo chroot "$ROOTFS_DIR" /bin/bash -lc '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt update
apt install -y \
  gcc g++ build-essential cmake pkg-config \
  libc6-dev libstdc++-14-dev \
  libopencv-dev opencv-data ffmpeg \
  es-sdk-log es-sdk-memory es-sdk-memcp es-sdk-cipher es-sdk-numa \
  es-sdk-common es-sdk-video-utils es-sdk-sys es-sdk-video es-hae \
  es-video-common es-mpp es-sdk-npu es-sdk-dsp es-sdk-audio \
  es-sdk-audio-codec es-sdk-ak es-sdk-sample-npu-runtime
cd /usr/include
ln -sfn riscv64-linux-gnu/bits bits
ln -sfn riscv64-linux-gnu/sys sys
ln -sfn riscv64-linux-gnu/gnu gnu
ln -sfn riscv64-linux-gnu/asm asm
# Do not copy /usr/lib/riscv64-linux-gnu into /usr/lib: it can fill the ext4 image.
# The cross build passes -B and -rpath-link to this multiarch directory instead.
'

echo "chroot dependencies installed in $ROOTFS_DIR"
