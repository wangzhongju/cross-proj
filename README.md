# cross-proj 双架构交叉编译环境

本仓库在一套 Ubuntu 24.04 Docker 容器中提供两条彼此隔离的交叉编译链：

- **riscv64 / EIC7700**：使用 `/opt/riscv` 工具链，并将 ESWIN SDK 生成的 `.ext4` 或 `.img` rootfs 挂载为编译 sysroot。
- **aarch64 / RK3588**：使用瑞芯微 SDK 的 GCC 10.3.1、与 RK3588 Ubuntu 22.04 运行环境一致的 sysroot，以及 ARM 平台第三方依赖。
- 工程以 `--target riscv64` 或 `--target aarch64` 选择架构，构建目录分别为 `build-riscv64/` 和 `build-aarch64/`。

## 快速开始

92 服务器项目根目录执行：

```bash
cd /home/cdky/workspace/github/cross-proj

# 默认使用 ESWIN SDK 202606；启动的同一容器也支持 RK3588 aarch64
./docker/docker.sh start

# 切换旧版 riscv64 SDK；aarch64 能力保持可用
SDK_VERSION=202507 ./docker/docker.sh start

./docker/docker.sh init
```

`SDK_VERSION=202507` 内部映射到目录 `eswin-sdk-20250730/`；`202606` 使用 `eswin-sdk-202606-ubuntu/`。容器内工作区固定为 `/workspace`。

## 常用编译命令

```bash
# aarch64 / RK3588
cd /workspace/proj/media-agent
./scripts/build.sh --target aarch64 --jobs 8 --install

# riscv64 / EIC7700，先完成 rootfs 挂载和 sysroot 链接
cd /workspace
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 deps
source ./scripts/source_sdk_env.sh P550
cd /workspace/proj/media-agent
./scripts/build.sh --target riscv64 --jobs 8 --install
```

默认构建类型为 `Release`。安装目录分别是：

- `proj/media-agent/build-aarch64/install/`
- `proj/media-agent/build-riscv64/install/`

## 目录职责

| 目录 | 职责 |
| --- | --- |
| `docker/` | 唯一的容器镜像、Compose 和启动入口。 |
| `packages/` | 本地 deb 包，例如 supertuxkart 和 supertuxkart-data。 |
| `scripts/` | RISC-V SDK 选择、系统镜像构建、挂载、chroot、sysroot 和验证流程。 |
| `eswin-sdk-20250730/` | ESWIN SDK 20250730。 |
| `eswin-sdk-202606-ubuntu/` | ESWIN SDK 20260630。 |
| `rk3588-sdk/` | 瑞芯微官方工具链、aarch64 sysroot、平台依赖及四个编译相关脚本。 |
| `proj/` | 使用交叉编译环境构建的业务工程。 |
| `docs/update.md` | 统一更新和验证记录，docs下存放交叉编译工程proj下所有工程的研发过程记录文档。 |

完整环境创建、挂载、chroot 扩展、编译及清理步骤见 [run.md](run.md)。RK3588 脚本说明见 [rk3588-sdk/scripts/README.md](rk3588-sdk/scripts/README.md)。

