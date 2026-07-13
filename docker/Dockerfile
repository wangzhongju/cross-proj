FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    sudo bash-completion vim cmake lsb-release pkg-config \
    gdisk dosfstools build-essential libncurses-dev gawk flex bison \
    openssl libssl-dev tree dkms libelf-dev libudev-dev libpci-dev \
    libiberty-dev autoconf automake autotools-dev ninja-build make \
    device-tree-compiler xz-utils devscripts ccache debhelper wget curl \
    pahole libconfuse-dev mtools rsync debootstrap qemu-user-static \
    qemu-utils binfmt-support mmdebstrap arch-test ca-certificates git file bc \
    kmod cpio python3 python3-pip python3-venv jq less locales \
    protobuf-compiler \
    android-sdk-platform-tools-common android-tools-adb android-tools-fastboot \
    && locale-gen en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

COPY riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz /tmp/toolchain.tar.xz
RUN tar -xf /tmp/toolchain.tar.xz -C /opt \
    && rm -f /tmp/toolchain.tar.xz \
    && /opt/riscv/bin/riscv64-unknown-linux-gnu-gcc --version

ENV PATH=/opt/riscv/bin:${PATH}
ENV ARCH=riscv
ENV CROSS_COMPILE=riscv64-unknown-linux-gnu-
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8
WORKDIR /workspace
CMD ["/bin/bash"]
