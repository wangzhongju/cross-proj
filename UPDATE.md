# UPDATE：P550 交叉编译环境从零构建记录

本文记录 92 服务器 `/home/cdky/workspace/github/cross-proj` 中 P550 交叉编译环境的工程化过程，包括系统镜像编译、交叉编译环境搭建、chroot 扩展依赖，以及已遇到问题的处理方式。

## 1. 基础条件

目标 SDK：`eswin-sdk-20250730`。目标工具链：`riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz`。目标开发板：`P550`。开发板 `192.168.88.104` 仅用于只读验证，本流程不修改开发板环境。

`packages/` 中准备了本地 deb：

```text
packages/supertuxkart_1.4+dfsg-4+b1rockos3_riscv64.deb
packages/supertuxkart-data_1.4+dfsg-4+b1rockos3_all.deb
```

## 2. Docker 镜像和容器

```bash
cd /home/cdky/workspace/github/cross-proj
./docker/docker.sh compile
./docker/docker.sh start
```

容器统一由 `docker/docker-compose-dev.yaml` 管理，`docker/docker.sh` 只做入口封装。容器基于 `ubuntu:24.04`，挂载当前工程到 `/workspace`，以宿主同 UID/GID 用户进入，并具备免密 sudo。SDK 生成 ext4、loop 挂载、binfmt 和 chroot 需要 privileged 容器。

## 3. SDK 源码和补丁

`u-boot` 与 `opensbi` 可从 `/home/cdky/workspace/EIC7700/cross-compile/eswin-sdk-20250730/source` 复制复用。所有源码修改统一执行：

```bash
cd /workspace
./scripts/patch_sdk_sources.sh
```

该脚本集中处理 U-Boot GCC 14 编译错误、kernel DSP 驱动声明和指针转换、KVM GCC internal compiler error、`pnmtologo` host 编译错误、kernel deb 固定包名问题，以及 `mkrootfs.sh` 中 supertuxkart 本地 deb 安装。

## 4. 编译系统镜像

桌面系统：

```bash
cd /workspace
./scripts/patch_sdk_sources.sh
source ./scripts/source_sdk_env.sh P550
cd /workspace/eswin-sdk-20250730
make_desktop_images
```

精简系统：

```bash
cd /workspace
./scripts/build_minimal_system.sh P550
```

生成物位于 `/workspace/eswin-sdk-20250730/P550/output`，包括 `root-P550-*.ext4`、`boot-P550-*.ext4` 和 kernel deb。

## 5. supertuxkart 本地 deb 安装问题

构建桌面 rootfs 时，镜像源中的 `supertuxkart-data_1.4+dfsg-4+b1rockos3_all.deb` 曾反复报：

```text
lzma error: compressed data is corrupt
unexpected end of file or stream
```

`packages/` 中准备的本地 deb 本身没有损坏，已通过 `dpkg-deb -x` 完整解压验证。问题出在安装路径：如果在 riscv64 chroot 内用 `apt install /root/local-packages/supertuxkart-data_*.deb` 或 `dpkg -i /root/local-packages/supertuxkart-data_*.deb`，大约 600MB 的 xz payload 仍然由 qemu-user 执行的 riscv64 解包链路处理，容易被截断并表现为 `lzma corrupt`。

最终处理方式：

1. `mkrootfs.sh` 仍会把 `/workspace/packages/supertuxkart*.deb` 复制到 rootfs 的 `/root/local-packages/`，便于追踪来源。
2. 对 `supertuxkart-data` 这类 `Architecture: all` 且没有 maintainer scripts 的大数据包，在 chroot 外用容器原生 `dpkg --root=<rootfs> --force-architecture -i <deb>` 安装，绕开 qemu-user。
3. 再进入 chroot 执行 `apt install -f -y` 配置依赖，并安装 riscv64 的 `supertuxkart` 主包。

已生成但 dpkg 状态不健康的 rootfs 使用：

```bash
cd /workspace
./scripts/repair_desktop_rootfs.sh P550
./scripts/chroot_exec.sh P550 'dpkg --audit && apt-get check'
```

`/etc/powervr.ini` 保留：

```ini
[supertuxkart]
DisableFBCDC=1
```


## 6. 先扩容再挂载

为了避免 chroot 安装 OpenCV、ESSDK、yaml-cpp 等依赖时 rootfs 空间不足，`chroot_mount.sh` 在挂载前会扩容 ext4 文件。默认 rootfs 至少 10G，boot 至少 500M。

```bash
cd /workspace
./scripts/chroot_mount.sh mount P550
```

可通过环境变量调整：

```bash
ROOTFS_MIN_SIZE=12G BOOT_MIN_SIZE=600M ./scripts/chroot_mount.sh mount P550
```

挂载后会绑定 `/dev`、`/dev/pts`、`/proc`、`/sys`、`/run`、`/tmp` 和 `/workspace`。

## 7. chroot 扩展依赖

执行任意 chroot 命令：

```bash
./scripts/chroot_exec.sh P550 'apt update'
./scripts/chroot_exec.sh P550 'apt install -y libyaml-cpp-dev'
```

安装 yolov5s 交叉编译依赖：

```bash
./scripts/install_chroot_deps.sh P550
```

不要把整个 `/usr/lib/riscv64-linux-gnu` 复制到 `/usr/lib`，否则会快速填满 rootfs。当前方案通过 `-B` 和 `-rpath-link` 使用 Debian multiarch 路径。

## 8. 使用 rootfs 作为 sysroot

```bash
./scripts/use_rootfs_sysroot.sh link P550
```

效果：

```text
/opt/riscv/sysroot -> /workspace/eswin-sdk-20250730/P550/output/rootfs
```

恢复：

```bash
./scripts/use_rootfs_sysroot.sh restore P550
```

总入口：

```bash
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 deps
./scripts/riscv_env_setup.sh /workspace P550 exec 'apt install -y libyaml-cpp-dev'
./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

## 9. 交叉编译 yolov5s

```bash
cd /workspace
source ./scripts/source_sdk_env.sh P550
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 deps
cd /workspace/yolov5s/src
./build.sh /opt/riscv/sysroot
file build/sample_npu
riscv64-unknown-linux-gnu-readelf -h build/sample_npu | grep -E 'Machine|Flags'
```

期望输出是 RISC-V 64 位可执行文件，`Machine` 为 `RISC-V`。

## 10. 问题处理清单

- SDK `setenv.sh`/`make_all` 与 `set -u` 不兼容：构建脚本在调用 SDK 函数前关闭 nounset。
- U-Boot GCC 14 错误：通过 `patch_uboot()` 修复函数签名、CRC 参数、指针转换和缺失头文件。
- Kernel DSP 驱动错误：通过 `patch_kernel()` 补声明、补头文件、修正 ioctl 指针和 power management 调用。
- GCC internal compiler error：P550 构建禁用 `CONFIG_KVM`，`pnmtologo.o` 使用 `-O0`。
- kernel deb 固定 6.6.18：改为安装 `../output/*.deb`，适配实际生成的 `6.6.92-eic7x-2025.07`。
- loop 设备不足：只补充 `/dev/loop27..63` 这类设备节点，不卸载无关宿主 loop。
- qemu-riscv64 binfmt 缺失：在构建环境注册 `/usr/lib/binfmt.d/qemu-riscv64.conf`，不修改开发板。
- rootfs 空间不足：挂载前自动扩容 rootfs。

## 11. 清理和验证

清理：

```bash
cd /workspace
./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

验证：

```bash
./docker/docker.sh status
./scripts/chroot_mount.sh status P550
./scripts/chroot_exec.sh P550 'dpkg --audit && apt-get check'
file /workspace/yolov5s/src/build/sample_npu
riscv64-unknown-linux-gnu-readelf -h /workspace/yolov5s/src/build/sample_npu | grep -E 'Machine|Flags'
```


## 12. make_desktop_images 中断后再次执行失败的可复现原因与修复

### 12.1 现象

SSH 断线或前台构建被中断后，再次执行 `make_desktop_images` 可能出现：

```text
W: binfmt_misc not found in /proc/mounts -- not mounted?
update-binfmts: warning: qemu-riscv64 not in database of installed binary formats.
E: riscv64 can neither be executed natively nor via qemu user emulation with binfmt_misc
ls: cannot access 'rootfs/boot/': No such file or directory
mount: rootfs/boot: mount point does not exist.
Mount failure
The system has reached the upper limit of available loop devices.
```

### 12.2 根因

该问题不是单一错误，而是三个状态叠加：

1. compose 容器重建或重新进入后，容器内 `/proc/sys/fs/binfmt_misc` 没有挂载，`qemu-riscv64` 没有注册。宿主机本身没有 qemu-riscv64 的 binfmt 配置文件，但容器内有 `/usr/lib/binfmt.d/qemu-riscv64.conf`，所以不能依赖宿主 `update-binfmts`。
2. riscv64 rootfs 不能通过 qemu-user 执行后，`mmdebstrap` 没有完整创建 rootfs，导致 `rootfs/boot` 不存在，后续 boot.ext4 挂载失败。
3. 前一次中断没有执行 SDK 脚本末尾的 `unmount_image`，会留下指向 `/workspace/eswin-sdk-20250730/.../mkimg-eswin/root-*.ext4` 的 loop 设备和 rootfs 挂载。再次构建时又在同一个 rootfs 挂载点叠挂，最终表现为 loop 设备不足和 60 秒重试。

### 12.3 固化修复

新增脚本：

```bash
./scripts/prepare_desktop_build_env.sh
```

用法：

```bash
./scripts/prepare_desktop_build_env.sh setup    # 构建前准备
./scripts/prepare_desktop_build_env.sh status   # 查看 binfmt、项目 loop、项目挂载和构建进程
./scripts/prepare_desktop_build_env.sh recover  # 中断后恢复，只清理本工程残留后重新 setup
```

`recover` 会先检查是否存在当前 SDK 构建进程；如果存在则拒绝清理，避免误清理仍在运行的构建。它只匹配 `/workspace/eswin-sdk-20250730` 和 `/home/cdky/workspace/github/cross-proj` 路径下的挂载与 loop，不会卸载宿主其它 loop。

同时 `docker/docker.sh start` 已经在启动 compose 容器后自动执行：

```bash
/workspace/scripts/prepare_desktop_build_env.sh setup
```

### 12.4 SDK 脚本补强

`patch_sdk_sources.sh` 会继续补丁 `mkrootfs.sh` 和 `mkrootfs_minimal.sh`：

- `mkdir "$CHROOT_TARGET"` 改为 `mkdir -p "$CHROOT_TARGET"`，避免中断后 rootfs 目录残留导致重跑失败。
- 挂载 boot.ext4 前增加 `mkdir -p "$CHROOT_TARGET"/boot`，避免 `rootfs/boot` 不存在时报错。

所有这些 SDK 源码改动仍然集中在 `patch_sdk_sources.sh` 中维护。

### 12.5 推荐重跑流程

```bash
cd /home/cdky/workspace/github/cross-proj
./docker/docker.sh start
./docker/docker.sh init

cd /workspace
./scripts/prepare_desktop_build_env.sh recover
./scripts/patch_sdk_sources.sh
source ./scripts/source_sdk_env.sh P550
cd /workspace/eswin-sdk-20250730
make_desktop_images
```

如果担心 SSH 再次断线，建议在 `tmux` 中执行，或用 `nohup` 写日志后后台运行。


## 14. 多版本交叉编译验证流程收敛

### 14.1 目标调整

按最终验证要求，`run.readme` 中不再保留宿主机一行 `docker.sh init '...'` 命令，只保留进入容器后的执行流程。交叉编译验证目标调整为：

- 20250730：使用 `yolov5s/src` 编译 `sample_npu`。
- 202606：使用 `proj/media-agent/third_party/algorithm` 编译 RISC-V algorithm 产物。

### 14.2 脚本更新

新增 `scripts/verify_cross_compile.sh` 作为容器内验证入口：

```bash
cd /workspace
unset SDK_VERSION
./scripts/verify_cross_compile.sh /workspace P550

export SDK_VERSION=202606
./scripts/verify_cross_compile.sh /workspace P550
```

该脚本会自动执行：

1. `riscv_env_setup.sh setup`：挂载 SDK 产物 rootfs，并链接 `/opt/riscv/sysroot`。
2. `riscv_env_setup.sh deps`：补齐 chroot/rootfs 中的开发依赖。
3. `source_sdk_env.sh`：加载对应版本 SDK 的交叉编译环境。
4. 按 SDK 版本选择验证目标。
5. `file` 与 `readelf -h` 检查 RISC-V 产物。
6. 退出时自动 `cleanup`，恢复原工具链 sysroot 并卸载 rootfs/loop。

支持调试开关：

- `KEEP_MOUNT=1`：验证结束后保留挂载。
- `SKIP_DEPS=1`：跳过依赖安装，用于 rootfs 已准备完成后的快速复跑。

`sdk_common.sh` 增加 `20260630 -> 202606` 兼容别名，仍解析到 `eswin-sdk-202606-ubuntu`。

### 14.3 202606 chroot DNS 与 apt 源问题

202606 Ubuntu rootfs 中 `/etc/resolv.conf` 是指向 `../run/systemd/resolve/stub-resolv.conf` 的符号链接。挂载脚本绑定 `/run` 后，该目标文件在 chroot 中可能不存在，导致：

```text
Temporary failure resolving 'ports.ubuntu.com'
Temporary failure resolving 'archive.eswincomputing.com'
```

`chroot_mount.sh` 已增加文件级 bind mount：挂载时把容器 `/etc/resolv.conf` 绑定到 rootfs 中 `resolv.conf` 的真实目标，卸载时先解除该文件 bind，再卸载 `/run`。

DNS 修复后，92 服务器访问官方 `ports.ubuntu.com:80` 仍会超时；清华、USTC、阿里 `ubuntu-ports` 镜像可达。`install_chroot_deps.sh` 已更新：

- 默认将 `ports.ubuntu.com/ubuntu-ports` 替换为 `http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports`。
- apt 使用 `Acquire::ForceIPv4=true`，避免优先 IPv6 时出现 `Network is unreachable`。
- 可通过 `APT_UBUNTU_PORTS_MIRROR` 覆盖镜像源。

### 14.4 验证结果

20250730 最终脚本验证通过：

```text
build/sample_npu: ELF 64-bit LSB executable, UCB RISC-V, RVC, double-float ABI
Machine: RISC-V
Flags: 0x5, RVC, double-float ABI
```

202606 最终脚本验证通过：

```text
build-riscv64/eic7700Infer/libcdky_eic7700_infer.so: ELF 64-bit LSB shared object, UCB RISC-V, RVC, double-float ABI
Machine: RISC-V
Flags: 0x5, RVC, double-float ABI
```

验证后 `riscv_env_setup.sh status` 确认两套 SDK 均无残留挂载。


## 15. algorithm 交叉编译增加 `-t` 后测试目标构建失败

### 15.1 现象

在 202606 验证流程中将 algorithm 编译命令改为：

```bash
./build.sh --target riscv64 --sysroot /opt/riscv/sysroot -q -t
```

普通库目标可以通过，但测试目标构建阶段失败，典型报错包括：

```text
fatal error: libavcodec/bsf.h: No such file or directory
No rule to make target '/opt/riscv/sysroot/usr/lib/riscv64-linux-gnu/libavformat.so'
warning: libyaml-cpp.so.0.8 ... not found
undefined reference to YAML::...
undefined reference to tbb::...
undefined reference to GDAL...
```

### 15.2 原因

- `-t` 会设置 `BUILD_TEST=1`，交叉编译时仍会构建测试可执行文件，只是默认跳过 `ctest` 运行。
- 202606 rootfs 里原来只有 FFmpeg runtime 包，缺少 `libavformat-dev/libavcodec-dev/libavutil-dev` 等开发头文件和 `.so` 开发链接。
- `cdky_track` 与 `cdky_event` 使用 yaml-cpp，但依赖声明为 `PRIVATE`，测试可执行文件链接这些 shared library 时拿不到 yaml-cpp。
- EIC7700/OpenCV 测试可执行文件在交叉链接阶段会触发目标机 OpenCV 间接依赖解析；这些目标机运行库不一定都在编译容器的链接搜索路径中。交叉验证默认不运行这些测试，因此可允许 shared library 的运行时未解析符号留到板端环境处理。

### 15.3 固化修复

- `install_chroot_deps.sh` 增加 FFmpeg dev 包：

```text
libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
```

- `byteTrack/cmake/srcs.cmake` 与 `eventEdge/cmake/srcs.cmake` 将 yaml-cpp 改为 `PUBLIC` 依赖。
- algorithm 顶层 CMake 增加 `algorithm_relax_cross_test_link()`，交叉编译测试可执行文件时添加：

```text
LINKER:--allow-shlib-undefined
```

- `tests/CMakeLists.txt` 和 `eic7700Infer/CMakeLists.txt` 对测试目标调用该函数。

### 15.4 验证结果

202606 带 `-t` 的验证通过：

```text
Built target common_config_test
Built target eic7700_edgeinfer_api_test
Built target tracker_api_test
Built target event_api_test
Built target eic7700_stream_infer_test
-- skip ctest for riscv64 cross build; run target tests on the board or set RUN_CROSS_TESTS=1.
build-riscv64/eic7700Infer/libcdky_eic7700_infer.so: ELF 64-bit LSB shared object, UCB RISC-V
Machine: RISC-V
Flags: 0x5, RVC, double-float ABI
```

## 14. 202606 Ubuntu SDK 多版本搭建记录

2026-07-15 在 92 服务器项目根目录新增官方 SDK：`/home/cdky/workspace/github/cross-proj/eswin-sdk-202606-ubuntu`。新版 SDK 与 20250730 的主要差异：

- SDK 目录名为 `eswin-sdk-202606-ubuntu`，`setenv.sh` 中 P550 菜单项变为 `[1] eic7700-hifive-premier-p550`。
- 新版不再提供 `source/mkimg-eswin`，系统镜像流程改为 `source/risc-v-gadget` + SDK 自带的 x86_64 `ubuntu-image`。
- P550 输出目录从旧版 `P550/output` 变为新版 `eic7700-hifive-premier-p550/output`。
- 生成物从旧版 `root-*.ext4`、`boot-*.ext4` 改为新版 Ubuntu preinstalled `.img`。

为支持多版本 SDK，新增 `scripts/sdk_common.sh`，统一解析 `SDK_VERSION`、`SDK_DIR`、实际 board 目录和输出目录。默认仍为 `20250730`；切换新版时使用：

```bash
export SDK_VERSION=202606
cd /workspace
./scripts/patch_sdk_sources.sh
source ./scripts/source_sdk_env.sh P550
cd "$SDK_DIR"
make_all
```

也可以显式指定目录：

```bash
SDK_DIR=/workspace/eswin-sdk-202606-ubuntu ./scripts/patch_sdk_sources.sh
SDK_DIR=/workspace/eswin-sdk-202606-ubuntu source ./scripts/source_sdk_env.sh P550
```

Docker 入口同步支持版本选择：

```bash
SDK_VERSION=202606 ./docker/docker.sh compile
SDK_VERSION=202606 ./docker/docker.sh start
SDK_VERSION=202606 ./docker/docker.sh init 'cd /workspace && ./scripts/build_minimal_system.sh P550'
```

新版官方 README 要求 `tomli`、`snapd`、`ubuntu-dev-tools`、`germinate` 等工具；`docker/Dockerfile` 已补齐 `python3-tomli snapd ubuntu-dev-tools germinate`。SDK 自带 `source/risc-v-gadget/ubuntu-image` 是 x86_64 静态可执行文件，构建镜像时由 SDK 直接调用。

源码补丁仍集中在 `patch_sdk_sources.sh`。202606 没有 `mkimg-eswin` 时该部分自动跳过；如果存在 `source/risc-v-gadget`，脚本会补丁 `customize-es.sh`：将 chroot 内的 `sudo tee /usr/share/initramfs-tools/hooks/no-ai-drivers` 改为 root 直接 `cat > ...`，避免目标 rootfs 未安装 `sudo` 时镜像定制失败。

挂载脚本已兼容两种输出：

- 20250730：继续查找并扩容 `root-${MODEL}-*.ext4`、`boot-${MODEL}-*.ext4`。
- 202606：若没有 ext4 文件，则查找最新 `.img`，通过 `losetup -P` 挂载 ext4 root 分区和 vfat EFI 分区，再绑定 `/dev`、`/proc`、`/sys`、`/run`、`/tmp` 与 `/workspace`。

日常命令示例：

```bash
SDK_VERSION=202606 ./scripts/chroot_mount.sh status P550
SDK_VERSION=202606 ./scripts/riscv_env_setup.sh /workspace P550 setup
SDK_VERSION=202606 ./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

### 14.1 202606 首次 bootchain 权限问题

首次执行：

```bash
SDK_VERSION=202606 ./docker/docker.sh init 'cd /workspace && ./scripts/build_minimal_system.sh P550'
```

在 `make_bootchain` 的 U-Boot `hifive_premier_p550_defconfig` 阶段失败：

```text
sh: 1: ./scripts/gcc-version.sh: Permission denied
sh: 1: ./scripts/clang-version.sh: Permission denied
Kconfig:66: syntax error
```

原因是 202606 SDK 的 `source/uboot-eswin/scripts/*.sh` 权限缺少 executable bit，Kconfig 无法执行版本检测脚本。处理方式已写入 `patch_sdk_sources.sh`：`patch_uboot()` 和 `patch_kernel()` 会对各自 `scripts/*.sh` 统一 `chmod a+x`，并且会同时处理 SDK 构建过程中拷贝到 board 目录下的 `uboot-eswin`、`linux-eswin`。

### 14.2 U-Boot 符号链接占位文件问题

修复脚本权限后，U-Boot 继续编译到 `cmd/setexpr.o` 时失败：

```text
include/ctype.h:1:1: error: expected identifier or '(' before numeric constant
    1 | linux/ctype.h
```

检查发现 `source/uboot-eswin/include/ctype.h` 是普通文本文件，内容仅为 `linux/ctype.h`，实际应为指向 `include/linux/ctype.h` 的符号链接。该问题会随 SDK 拷贝传播到 `eic7700-hifive-premier-p550/uboot-eswin`。处理方式同样写入 `patch_sdk_sources.sh`：`patch_uboot()` 检测到该占位文件后删除并重建为 `include/ctype.h -> linux/ctype.h` 符号链接。

### 14.3 python3 alternatives 问题

U-Boot 和 OpenSBI 成功生成后，进入 `make_kernel()` 前失败：

```text
update-alternatives: error: no alternatives for python3
```

202606 `setenv.sh` 在 `make_kernel()` 和 `make_images()` 中分别执行 `echo 2 | sudo update-alternatives --config python3`、`echo 1 | sudo update-alternatives --config python3`。Ubuntu 24.04 容器默认只有 `/usr/bin/python3 -> python3.12`，没有 `python3` alternatives 组，所以该命令失败。处理方式：

- `docker/Dockerfile` 增加 `update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 1`，新建镜像时自动注册 alternatives。
- 当前已运行的 `cross-proj-202606-cdky` 容器中也执行同一条命令，便于继续本轮构建。
- `patch_sdk_sources.sh` 增加 `patch_setenv()`，将 202606 `setenv.sh` 中交互式 `echo N | sudo update-alternatives --config python3` 改为非交互 `sudo update-alternatives --set python3 /usr/bin/python3.12 >/dev/null 2>&1 || true`。
- `build_minimal_system.sh` 调整为 source SDK 环境前先执行一次补丁，确保 `setenv.sh` 的函数体补丁能进入当前 shell；source 后再执行一次补丁，继续覆盖已存在的 board 拷贝目录。

### 14.4 202606 linux source 残缺目录问题

继续进入 `make_kernel()` 后失败：

```text
cat: /workspace/eswin-sdk-202606-ubuntu/eic7700-hifive-premier-p550/linux-eswin/Makefile: No such file or directory
```

检查发现 `source/linux-eswin` 目录存在，但缺少 Linux kernel 顶层 `Makefile`、`Kconfig` 和 `.git`，属于残缺 source 目录。由于官方 `setenv.sh` 的逻辑是“目录不存在才 `git clone -b ${RELEASE_TAG}`”，该残缺目录会阻止按官方 tag 重新拉取完整源码。

注意：`EIC7X-26.06` 是 tag，不是 branch；`git clone -b EIC7X-26.06 https://github.com/eswincomputing/linux-stable.git ...` 对 tag 有效。处理方式写入 `patch_sdk_sources.sh`：若检测到 `$SDK_DIR/source/linux-eswin` 存在但没有顶层 `Makefile`，则移动为 `source/linux-eswin.incomplete-YYYYmmdd-HHMMSS` 备份，让后续 `make_kernel()` 触发官方脚本按 `EIC7X-26.06` tag 重新 clone。

后续用户已修正 `source/linux-eswin` 缺失问题。当前完整目录约 1.9G，`git describe --tags --always` 显示 `EIC7X-26.06`，顶层 `Makefile` 显示 kernel `6.6.138`，并包含 `kernel/`、`fs/`、`net/`、`tools/`、`usr/` 等完整目录。

### 14.5 DSP ioctl IOMMU 头文件问题

kernel 进入 `dpkg-buildpackage` 后，`vmlinux` 阶段失败：

```text
drivers/soc/eswin/ai_driver/dsp/dsp_ioctl.c:172:18: error: implicit declaration of function 'iommu_get_domain_for_dev'
drivers/soc/eswin/ai_driver/dsp/dsp_ioctl.c:179:29: error: implicit declaration of function 'iommu_iova_to_phys'
```

`include/linux/iommu.h` 中存在这两个 API 的声明，但 `dsp_ioctl.c` 没有 include 该头文件。`patch_sdk_sources.sh` 已更新：`patch_kernel()` 会在 `#include <linux/dma-direct.h>` 后补入 `#include <linux/iommu.h>`。

### 14.6 DSP platform dma_addr_t 初始化问题

继续编译后，`dsp_platform.c` 报：

```text
drivers/soc/eswin/ai_driver/dsp/dsp_platform.c:1265:22: error: assignment to 'dma_addr_t' from 'void *' makes integer from pointer without a cast
    hw->pts_iova = NULL;
```

`hw->pts_iova` 是 `dma_addr_t`，不是指针。`patch_sdk_sources.sh` 已更新：`patch_kernel()` 将 `hw->pts_iova = NULL;` 改为 `hw->pts_iova = 0;`。

### 14.7 DSP mloader 指针/整数位运算问题

继续编译后，`xt_mld_loader.c` 报：

```text
drivers/soc/eswin/ai_driver/dsp/mloader/xt_mld_loader.c:218:34: error: invalid operands to binary | (have 'xtmld_ptr' {aka 'void *'} and 'long unsigned int')
```

202606 版本在 `align_ptr()` 中先把高位地址片段保存为 `xtmld_ptr`，随后执行 `ret_hi | (unsigned long)ret`，导致左操作数仍是 `void *`。`patch_sdk_sources.sh` 已更新：`patch_kernel()` 会把该路径下的地址拼接中间值统一改为 `unsigned long`，并同时兼容 20250730 与 202606 两种源码写法。

### 14.8 make_all 跳过 make_kernel 问题

202606 `setenv.sh` 中的 `make_all()` 只调用：

```bash
make_bootchain
make_images
```

而 `make_images()` 只有在 `${board_name}/linux-eswin/eswin.toml` 不存在时才会补跑 `make_kernel`。如果 kernel 曾经失败过，`eswin.toml` 已经生成但完整 kernel deb 没有生成，后续再跑 `make_all()` 会直接进入 `make_images()`，并报：

```text
error!!!
must to build kernel first!!!
```

`patch_sdk_sources.sh` 已更新：`patch_setenv()` 会把 `make_all()` 调整为 `make_bootchain -> make_kernel -> make_images`，确保从工程入口执行 `build_minimal_system.sh` 时每次都会重新构建 kernel。

### 14.9 DSP mloader relocation 参数类型问题

继续执行 `make_kernel` 后，`xt_mld_relocate.c` 报：

```text
drivers/soc/eswin/ai_driver/dsp/mloader/xt_mld_relocate.c:348:37: error: passing argument 3 of 'reloc_addr_value' from incompatible pointer type
drivers/soc/eswin/ai_driver/dsp/mloader/xt_mld_relocate.c:359:38: error: passing argument 3 of 'reloc_addr_value' from incompatible pointer type
drivers/soc/eswin/ai_driver/dsp/mloader/xt_mld_relocate.c:383:61: error: passing argument 3 of 'reloc_addr_value' from incompatible pointer type
```

`reloc_addr_value()` 第三个参数类型是 `Elf32_Addr *`，但部分调用传入了 `xtmld_ptr *`。`patch_sdk_sources.sh` 已更新：`patch_kernel()` 会将这些重定位中间变量改为 `Elf32_Addr`，并去掉 `((xtmld_ptr *)&val)` 这类不匹配强转。

### 14.10 202606 make_images 验证结果

完成上述修复后，分段执行：

```bash
SDK_VERSION=202606 ./docker/docker.sh init 'cd /workspace && SDK_VERSION=202606 ./scripts/patch_sdk_sources.sh && source ./scripts/source_sdk_env.sh P550 && cd /workspace/eswin-sdk-202606-ubuntu && make_kernel'
SDK_VERSION=202606 ./docker/docker.sh init 'cd /workspace && SDK_VERSION=202606 ./scripts/patch_sdk_sources.sh && source ./scripts/source_sdk_env.sh P550 && cd /workspace/eswin-sdk-202606-ubuntu && make_images'
```

验证通过，输出目录生成：

```text
linux-image-6.6.138-2026-eic7700_6.6.138-2026.07.15_riscv64.deb
linux-modules-6.6.138-2026-eic7700_6.6.138-2026.07.15_riscv64.deb
linux-headers-6.6.138-2026-eic7700_6.6.138-2026.07.15_riscv64.deb
eic7700-hifive-premier-p550-ubuntu-24.04-preinstalled-server-riscv64-20260715-174215.img
```

`ubuntu-image` 阶段日志中仍会出现 locale、`/dev/pts` apt log、chroot 内服务启动被拒绝等 warning，但本轮没有阻塞镜像生成。由于官方 `make_images()` 使用 `sudo mv` 移动 `.img`，首次产物可能是 `root:root`；`patch_sdk_sources.sh` 已更新：`patch_setenv()` 会在移动镜像后补 `chown "$(id -u):$(id -g)"`，避免后续产物归属 root。


## 13. locale、dbus、supertuxkart 本地 deb 未完成安装问题

### 13.1 现象

`make_desktop_images` 最终生成了 `boot.ext4` 和 `root.ext4`，但日志中可能出现：

```text
perl: warning: Setting locale failed.
locale: Cannot set LC_CTYPE to default locale
chown: cannot access '/home/eswin/*': No such file or directory
Failed to connect to socket /run/dbus/system_bus_socket
supertuxkart-data ... lzma error: compressed data is corrupt
```

如果继续使用这样的 rootfs，`dpkg --audit` 会发现 `supertuxkart` 处于半安装或未配置状态，后续再安装 `libqt5gui5-gles`、`python3-opencv` 等包时也会被 broken dependency 阻塞。

### 13.2 原因

- locale warning：SDK 原脚本在 `after_mkrootfs()` 后半段才生成 `en_US.UTF-8`，但前面的 kernel/u-boot-menu/perl 脚本已经使用 `LC_ALL=en_US.UTF-8`。
- chown warning：`/home/eswin` 刚创建时可能为空，`/home/eswin/*` 没有匹配文件。
- dbus warning：chroot 构建环境没有运行 system dbus，包安装脚本尝试 reload 服务会失败。
- supertuxkart-data：本地 deb 已复制到 rootfs，且 sha256 与 `packages/` 原文件一致；包本身也可在容器原生环境完整解压。失败原因是 riscv64 chroot 中的 apt/dpkg 通过 qemu-user 解压 600MB xz payload 时会截断，表现为压缩数据损坏。
- `dpkg --root` 细节：在 chroot 外用原生 dpkg 操作目标 rootfs 时，dpkg 会通过当前容器 NSS 解析 `var/lib/dpkg/statoverride` 中的用户/组。脚本会先把目标 rootfs statoverride 中涉及但容器缺失的 group 临时补齐，例如 `crontab`。

### 13.3 固化修复

`patch_sdk_sources.sh` 已更新：

- 在 `make_rootfs()` 的 `apt update` 后提前生成 `en_US.UTF-8`。
- 在 chroot 中创建临时 `/usr/sbin/policy-rc.d`，禁止包安装阶段启动或 reload 服务，构建结束前删除。
- 将 `chown eswin:eswin /home/eswin/*` 改为 `chown -R eswin:eswin /home/eswin || true`。
- 桌面相关 apt heredoc 增加 `set -e`，避免 apt 失败后继续生成看似完成但 dpkg 状态不健康的 ext4。
- 保留 `/root/local-packages` 中的 `supertuxkart` deb，便于后续修复脚本使用。

`repair_desktop_rootfs.sh` 已更新：

- 挂载最新 root/boot ext4。
- 复制 `packages/supertuxkart*.deb` 到 rootfs 的 `/root/local-packages/`。
- 在 chroot 外使用容器原生 `dpkg --root=<rootfs> --force-architecture -i <supertuxkart-data.deb>` 安装 `Architecture: all` 的大数据包，绕开 qemu-user。
- 进入 chroot 执行 `apt install -f -y`，安装 `supertuxkart` 主包和 `libqt5gui5-gles python3-opencv`，最后执行 `dpkg --configure -a` 与 `apt-get check`。

### 13.4 修复已生成 root.ext4

如果已经生成 ext4 但 dpkg 状态不健康，执行：

```bash
cd /workspace
./scripts/repair_desktop_rootfs.sh P550
./scripts/chroot_exec.sh P550 'dpkg --audit && apt-get check'
./scripts/chroot_exec.sh P550 'dpkg -l supertuxkart supertuxkart-data'
```

验证通过时应看到：

```text
ii  supertuxkart
ii  supertuxkart-data
```

### 13.5 重新构建推荐流程

```bash
cd /workspace
./scripts/prepare_desktop_build_env.sh recover
./scripts/patch_sdk_sources.sh
source ./scripts/source_sdk_env.sh P550
cd /workspace/eswin-sdk-20250730
make_desktop_images
```

如果担心 SSH 再次断线，建议在 `tmux` 中执行，或用 `nohup` 写日志后后台运行。
