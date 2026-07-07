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

