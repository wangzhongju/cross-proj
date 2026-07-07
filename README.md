# P550 交叉编译环境工程化说明

本仓库用于在 92 服务器上构建 P550 开发板的交叉编译环境。环境基于 `ubuntu:24.04` 自建 Docker 镜像、`eswin-sdk-20250730` SDK，以及 `riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz` 工具链。
> cd docker
> wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2025.01.20/riscv64-glibc-ubuntu-24.04-gcc-nightly-2025.01.20-nightly.tar.xz

## 目录结构

```text
docker/                         Dockerfile、docker-compose-dev.yaml、容器入口脚本
packages/                       本地 deb 包，例如 supertuxkart 和 supertuxkart-data
scripts/                        SDK 补丁、镜像挂载、chroot、sysroot 管理脚本
eswin-sdk-20250730/             ESWIN SDK 20250730
yolov5s/                        用于验证交叉编译的示例程序
run.readme                      快速命令清单
UPDATE.md                       从零构建和问题处理记录
```

## Docker 容器管理

容器统一由 `docker/docker-compose-dev.yaml` 管理，`docker/docker.sh` 只是命令入口，风格与 `docker_new` 保持一致。

```bash
cd /home/cdky/workspace/github/cross-proj
./docker/docker.sh compile        # 构建 cross-proj-p550:20250730 镜像
./docker/docker.sh start          # 启动并进入容器
./docker/docker.sh init           # 进入已启动容器
./docker/docker.sh init 'whoami'  # 在容器中执行命令
./docker/docker.sh status         # 查看镜像和容器状态
./docker/docker.sh stop           # 停止并删除 compose 容器
```

容器内工作目录是 `/workspace`，默认以宿主机同名用户进入，并具备免密 sudo 权限。

## SDK 源码补丁原则

`eswin-sdk-20250730/source` 以及 SDK 构建过程中复制到 `P550/` 下的源码，所有修改统一写入：

```bash
./scripts/patch_sdk_sources.sh
```

不要直接手工修改 SDK 源码文件。这样出现新问题时可以快速定位每一处变更，也便于重新解压 SDK 后重复应用。

## 构建系统镜像

```bash
cd /workspace
./scripts/patch_sdk_sources.sh
source ./scripts/source_sdk_env.sh P550
cd /workspace/eswin-sdk-20250730
make_desktop_images
```

如果只需要精简系统：

```bash
cd /workspace
./scripts/build_minimal_system.sh P550
```

生成物位于 `/workspace/eswin-sdk-20250730/P550/output`。

## 挂载 rootfs 并扩展依赖

挂载脚本会先检查 ext4 文件大小，再扩容到默认至少 10G，然后挂载 root/boot：

```bash
cd /workspace
./scripts/chroot_mount.sh mount P550
```

在 chroot 中安装额外依赖，例如 `yaml-cpp`：

```bash
./scripts/chroot_exec.sh P550 'apt update && apt install -y libyaml-cpp-dev'
```

安装 yolov5s 交叉编译所需依赖：

```bash
./scripts/install_chroot_deps.sh P550
```

清理挂载并恢复工具链 sysroot：

```bash
./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

## 交叉编译 yolov5s

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

期望结果为 RISC-V 64 位可执行文件，`Machine` 显示 `RISC-V`。

## 修复已生成的桌面 rootfs

如果 `make_desktop_images` 已经生成 `root.ext4`，但日志中出现 `supertuxkart-data` 解压失败或 `apt-get check` 不通过，可在容器内执行：

```bash
cd /workspace
./scripts/repair_desktop_rootfs.sh P550
./scripts/chroot_exec.sh P550 'dpkg --audit && apt-get check'
```

该脚本会对 `supertuxkart-data` 使用 chroot 外的 `dpkg --root` 安装，避免 qemu-user 解压大包失败。

## 详细过程

完整从零构建、系统镜像编译、chroot 扩展依赖、supertuxkart 本地 deb 安装、问题处理记录见 [UPDATE.md](UPDATE.md)。脚本作用说明见 [scripts/README.md](scripts/README.md)。
