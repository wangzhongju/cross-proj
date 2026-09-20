# 双架构交叉编译环境运行手册

## 1. 统一容器

### 1.1 环境组成

`docker/docker.sh start` 创建的容器同时具备：

- `/opt/riscv/bin/riscv64-unknown-linux-gnu-*`；
- `/workspace/rk3588-sdk/prebuilts/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-*`；
- QEMU user mode、binfmt、loop、mount、chroot、CMake、Debian 打包工具；
- `RK3588_SDK_ROOT=/workspace/rk3588-sdk`。

两套工具链可以同时存在。CMake toolchain 文件和工程的 `--target` 决定本次构建使用哪一套编译器和 sysroot。

首次构建 Docker 镜像前，确认 RISC-V 工具链压缩包存在：

```bash
cd /home/cdky/workspace/github/cross-proj/docker
test -f riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz || \
  wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2025.01.20/riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz
```

### 1.2 启动与版本切换

```bash
cd /home/cdky/workspace/github/cross-proj

# 默认：ESWIN SDK 202606 + RK3588 aarch64
./docker/docker.sh start

# 旧版 RISC-V SDK，202507 映射到 eswin-sdk-20250730
SDK_VERSION=202507 ./docker/docker.sh start

./docker/docker.sh status
./docker/docker.sh init
./docker/docker.sh stop
```

`start` 会删除旧的独立 `cross-proj-rk3588-<user>` 容器，启动 Compose 容器，初始化宿主同 UID 用户、QEMU/loop 条件及瑞芯微工具链兼容链接。默认 SDK 版本是 `202606`。

如 Dockerfile 或 Compose 配置发生变化，先执行：

```bash
./docker/docker.sh compile
./docker/docker.sh restart
```

## 2. RISC-V rootfs 的生成、挂载与扩展

### 2.1 生成镜像

SDK 202606 生成完整 Ubuntu disk image：

```bash
cd /workspace
export SDK_VERSION=202606
./scripts/patch_sdk_sources.sh
./scripts/build_minimal_system.sh P550
```

输出位于：

```text
eswin-sdk-202606-ubuntu/eic7700-hifive-premier-p550/output/*.img
```

旧版 SDK 使用独立 root/boot ext4：

```bash
cd /workspace
export SDK_VERSION=202507
./scripts/patch_sdk_sources.sh
./scripts/build_minimal_system.sh P550
```

输出位于：

```text
eswin-sdk-20250730/P550/output/root-P550-*.ext4
eswin-sdk-20250730/P550/output/boot-P550-*.ext4
```

### 2.2 挂载过程

推荐统一入口：

```bash
cd /workspace
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 status
```

底层步骤如下：

1. `scripts/sdk_common.sh` 根据 `SDK_VERSION` 解析 SDK、板型和 output。
2. 对独立 ext4，先用 `e2fsck`、`resize2fs` 扩容，再分别挂载 root 和 boot。
3. 对完整 `.img`，用 `losetup --find --show -P` 建立 loop 和分区节点，通过文件系统类型选出 ext4 根分区和 FAT boot 分区。
4. 将 `/dev`、`/dev/pts`、`/proc`、`/sys`、`/run`、`/tmp`、DNS 和 `/workspace` bind mount 到 rootfs。
5. `use_rootfs_sysroot.sh` 备份工具链原 sysroot，并将挂载的 rootfs 链接为 `/opt/riscv/sysroot`。

不要手工删除 loop 或挂载目录。中断后使用 `cleanup` 或 `prepare_desktop_build_env.sh recover`。

### 2.3 chroot 扩展依赖

容器已注册 qemu-riscv64 binfmt，因此 x86_64 容器可以进入 RISC-V rootfs：

```bash
./scripts/riscv_env_setup.sh /workspace P550 deps
./scripts/riscv_env_setup.sh /workspace P550 exec 'dpkg --audit && apt-get check'
./scripts/riscv_env_setup.sh /workspace P550 exec 'apt-get install -y libyaml-cpp-dev'
```

`deps` 会安装编译器运行库、FFmpeg、Protobuf 和 ESWIN SDK 依赖，并修正 RISC-V multiarch 头文件链接。依赖实际写入镜像中的 rootfs，因此以后重新挂载仍然存在。

清理顺序必须先恢复 `/opt/riscv/sysroot`，再卸载 bind mount、rootfs、boot 和 loop：

```bash
./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

## 3. RK3588 aarch64 rootfs 的同类实现

### 3.1 工具链

将瑞芯微 SDK 的完整目录放到：

```text
rk3588-sdk/prebuilts/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/
```

在完整瑞芯微 RK3588 Linux SDK 中，对应来源通常是：

```text
prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/
```

应复制整个目录，不能只复制 `bin/`；GCC 还需要内部 include、libexec、目标库和 specs。

本项目验证的固定提交为 `adbb295a970c4b39dc487c95226fe84d2c460072`。容器启动时自动执行：

```bash
/workspace/rk3588-sdk/scripts/setup-toolchain.sh
```

该脚本只在 `compat-bin/` 创建兼容工具名，不修改 `/opt/riscv`。

### 3.2 挂载官方 rootfs

应使用与 RK3588 板型、Ubuntu 版本和 glibc 一致的瑞芯微官方 SDK rootfs。以下展示与 RISC-V 相同的原理，实际镜像名和根分区号以官方 SDK 输出为准。

独立 ext4：

```bash
export RK_ROOTFS_IMAGE=/path/to/rk3588-rootfs.ext4
export RK_SYSROOT=/workspace/rk3588-sdk/sysroot
sudo mkdir -p "$RK_SYSROOT"
sudo e2fsck -fy "$RK_ROOTFS_IMAGE"
sudo mount -o loop "$RK_ROOTFS_IMAGE" "$RK_SYSROOT"
```

完整 `.img`：

```bash
export RK_ROOTFS_IMAGE=/path/to/rk3588-system.img
export RK_SYSROOT=/workspace/rk3588-sdk/sysroot
loopdev=$(sudo losetup --find --show -P "$RK_ROOTFS_IMAGE")
lsblk -f "$loopdev"
# 根据 lsblk 结果选择 ext4 根分区，不能直接假设分区号
sudo mkdir -p "$RK_SYSROOT"
sudo mount "${loopdev}pN" "$RK_SYSROOT"
```

当前 79 板运行 Ubuntu 22.04、glibc 2.35。用于编译的 sysroot 必须保持同一 ABI，不能用容器自身 Ubuntu 24.04 的 aarch64 库替代。

### 3.3 chroot 扩展

先准备 QEMU 和 bind mount：

```bash
sudo cp /usr/bin/qemu-aarch64-static "$RK_SYSROOT/usr/bin/"
for d in dev proc sys run; do
  sudo mount --bind "/$d" "$RK_SYSROOT/$d"
done
# Ubuntu 的 resolv.conf 常为绝对或相对符号链接，必须绑定到 rootfs 内的实际目标
resolv_link=$(readlink "$RK_SYSROOT/etc/resolv.conf" || true)
if [[ "$resolv_link" = /* ]]; then
  RK_RESOLV_TARGET="$RK_SYSROOT$resolv_link"
elif [[ -n "$resolv_link" ]]; then
  RK_RESOLV_TARGET=$(realpath -m "$RK_SYSROOT/etc/$resolv_link")
else
  RK_RESOLV_TARGET="$RK_SYSROOT/etc/resolv.conf"
fi
sudo mkdir -p "$(dirname "$RK_RESOLV_TARGET")"
sudo touch "$RK_RESOLV_TARGET"
sudo mount --bind /etc/resolv.conf "$RK_RESOLV_TARGET"
```

然后安装目标环境开发依赖：

```bash
sudo chroot "$RK_SYSROOT" /bin/bash -lc '
  apt-get update
  apt-get install -y build-essential cmake pkg-config \
    protobuf-compiler libprotobuf-dev libssl-dev libyaml-cpp-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev
'
```

瑞芯微 MPP/RGA 和工程依赖的 ARM 平台库应使用同一官方 SDK 的目标文件。可将官方 SDK 对应目录 bind mount 到 `rk3588-sdk/algorithm-thirdparty/arm/ubuntu22.04`，避免复制出另一份不可追踪的运行库。完成后确保：

```bash
test -x "$RK_SYSROOT/usr/bin/protoc"
test -f "$RK_SYSROOT/usr/lib/aarch64-linux-gnu/libprotobuf.so"
sudo ln -sfn libstdc++.so.6 "$RK_SYSROOT/usr/lib/aarch64-linux-gnu/libstdc++.so"
```

`board-protoc.sh` 通过 qemu 执行 sysroot 内的目标 `protoc`。目标生成器与目标 `libprotobuf` 应保持同一版本。

卸载时按相反顺序处理：

```bash
sudo umount "$RK_RESOLV_TARGET" || true
for d in run sys proc dev; do sudo umount -l "$RK_SYSROOT/$d" || true; done
sudo umount "$RK_SYSROOT"
# 使用 .img 时再执行：sudo losetup -d "$loopdev"
```

本流程不会生成或刷写设备镜像；生成镜像仍应按实际 Firefly 板级配置使用瑞芯微官方 SDK。

## 4. media-agent 如何选择交叉编译环境

`proj/media-agent/scripts/build.sh` 为每个目标选择独立 CMake toolchain：

| 参数 | CMake toolchain | 编译器 | sysroot | 构建目录 |
| --- | --- | --- | --- | --- |
| `--target riscv64` | `cmake/riscv64-toolchain.cmake` | `/opt/riscv/bin/riscv64-unknown-linux-gnu-*` | `/opt/riscv/sysroot` | `build-riscv64/` |
| `--target aarch64` | `cmake/aarch64-toolchain.cmake` | 瑞芯微 GCC 10.3.1 | `$RK3588_SDK_ROOT/sysroot` | `build-aarch64/` |

RISC-V 编译：

```bash
cd /workspace
export SDK_VERSION=202606
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 deps
source ./scripts/source_sdk_env.sh P550

cd /workspace/proj/media-agent
./scripts/build.sh --target riscv64 --jobs 8 --install
```

aarch64 编译：

```bash
cd /workspace/proj/media-agent
./scripts/build.sh --target aarch64 --jobs 8 --install
```

也可从宿主机直接触发带日志的入口：

```bash
./docker/docker.sh init '/workspace/rk3588-sdk/scripts/run-media-agent-build.sh 8'
```

默认构建类型为 `Release`。需要调试信息时显式传入 `--type RelWithDebInfo`；这会显著增大动态库。

可选 Debian 包：

```bash
cd /workspace/proj/media-agent
./scripts/package_deb.sh --target aarch64 --jobs 8 --install
```

该命令只生成包，不应在正在运行 `/opt/media_agent/media_agent` 的 79 板直接安装。安装包维护脚本可能停止服务、删除原安装目录并启动 systemd 服务。

## 5. libcdky_event.so 与 libcdky_track.so 大小

此前默认构建实际采用 `RelWithDebInfo`，aarch64 安装产物分别约为：

- `libcdky_event.so`：60 MiB；
- `libcdky_track.so`：24 MiB。

两个文件均显示 `with debug_info, not stripped`。仅对临时副本执行 `aarch64-none-linux-gnu-strip --strip-debug` 后分别约为 1.4 MiB 和 722 KiB，说明主要空间来自 DWARF 调试段和符号表，业务机器码不是主要来源。event/track 又静态链接各自内部实现，大量 C++ 模板实例会进一步增加调试信息。

工程默认构建类型已修正为 `Release`，与帮助文本一致。清除旧缓存重新生成后的实际结果为：

- `libcdky_event.so`：1,414,056 bytes，SHA-256 `10d283179ec7661c91afaa8e2a26ffc252ab5d404e781c7cb59280715739b6c7`；
- `libcdky_track.so`：741,032 bytes，SHA-256 `f8265f97680438f9344d5e4ae83cd9d150ef7fbcb764d060c1ebccf78e34960e`。

Release 产物仍保留普通符号表和极少量工具链生成的调试段，但已不包含原先数十 MiB 的完整 DWARF 信息。复现命令：

```bash
cd /workspace/proj/media-agent
./scripts/build.sh --target aarch64 --type Release --jobs 8 --clean --install
file build-aarch64/install/lib/libcdky_event.so
file build-aarch64/install/lib/libcdky_track.so
```

发布时若还需去掉普通符号表，应在保留独立调试文件后使用 toolchain 的 `strip --strip-unneeded`，并重新执行板端动态依赖和功能验证。

## 6. 92 内存紧张时处理 RagFlow

RagFlow Compose 目录为 `/home/cdky/workspace/github/ragflow/docker`。编译前记录当前运行服务，再停止相同服务；完成后恢复：

```bash
cd /home/cdky/workspace/github/ragflow/docker
compose=(docker compose -p ragflow-v0272 -f docker-compose.yml -f docker-compose.local92.yml)
"${compose[@]}" ps --services --status running > /tmp/ragflow-running-services.txt
mapfile -t services < /tmp/ragflow-running-services.txt
"${compose[@]}" stop --timeout 30 "${services[@]}"

# 编译完成后
"${compose[@]}" start "${services[@]}"
"${compose[@]}" ps
rm -f /tmp/ragflow-running-services.txt
```

不要使用 `docker compose down -v`，否则可能删除服务和数据卷。
