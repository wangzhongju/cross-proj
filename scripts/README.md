# scripts 脚本说明

本目录保留的是交叉编译环境从构建、挂载、chroot 扩展到 sysroot 切换的可重复入口。脚本数量看起来偏多，但边界按风险拆开：源码补丁、SDK 环境、镜像挂载、chroot 操作和 sysroot 链接分别独立，便于定位问题和回滚。

| 脚本 | 作用 | 是否保留 | 说明 |
| --- | --- | --- | --- |
| `patch_sdk_sources.sh` | 集中修复 `eswin-sdk-20250730/source` 和 SDK 拷贝出的 `P550/*` 源码 | 必须保留 | 所有 SDK 源码改动只能写到这里，避免手工修改难以追踪。 |
| `source_sdk_env.sh` | 自动选择 `setenv.sh` 的 P550 菜单并 source SDK 环境 | 必须保留 | 需要被 `source`，不能合并到普通执行脚本。 |
| `build_minimal_system.sh` | 构建 bootloader、kernel、minimal root/boot ext4 | 保留 | 封装 `make_all`，构建前自动执行补丁。 |
| `chroot_mount.sh` | 查找最新 root/boot ext4 或 `.img`，先扩容再挂载，并绑定 dev/proc/sys/run/tmp/workspace/resolv.conf | 保留 | 这是高风险主机操作，单独脚本更容易检查和清理；202606 rootfs 的 resolv.conf 符号链接也在这里统一修复。 |
| `chroot_exec.sh` | 在已挂载 rootfs 中执行命令或进入交互 chroot | 保留 | 作为 chroot 的轻量入口，也被总入口调用。 |
| `install_chroot_deps.sh` | 在 chroot 中安装交叉编译依赖和 ESSDK/NPU/OpenCV 依赖 | 保留 | 依赖安装经常需要单独重复执行，独立更清楚。 |
| `use_rootfs_sysroot.sh` | 将挂载后的 rootfs 链接为 `/opt/riscv/sysroot`，并支持恢复原 sysroot | 保留 | 涉及 `/opt/riscv`，独立脚本降低误操作风险。 |
| `riscv_env_setup.sh` | 总入口，整合 mount/deps/exec/sysroot/cleanup/status | 保留并增强 | 可作为日常入口，底层仍调用单职责脚本。 |
| `verify_cross_compile.sh` | 按 SDK 版本执行对应交叉编译验证 | 保留 | 20250730 编译 `yolov5s`；202606 编译 `proj/media-agent/third_party/algorithm -t`，并自动挂载/清理 sysroot。 |
| `prepare_desktop_build_env.sh` | 构建前准备和中断恢复：注册 qemu-riscv64 binfmt、补 loop 节点、清理本工程残留挂载/loop | 必须保留 | 专门处理 SSH 断线或构建中断后再次执行 `make_desktop_images` 的可复现问题。 |
| `repair_desktop_rootfs.sh` | 修复已生成桌面 root.ext4 中未完成的 locale、supertuxkart 和 apt 依赖状态 | 可选保留 | 对 `supertuxkart-data` 使用 chroot 外 `dpkg --root` 安装，绕开 qemu-user 解压大包失败。 |

## 推荐入口

日常使用优先调用 `riscv_env_setup.sh`：

```bash
./scripts/riscv_env_setup.sh /workspace P550 setup
./scripts/riscv_env_setup.sh /workspace P550 deps
./scripts/riscv_env_setup.sh /workspace P550 exec 'apt install -y libyaml-cpp-dev'
./scripts/riscv_env_setup.sh /workspace P550 cleanup
```

交叉编译验证优先调用；默认会先补齐 chroot/rootfs 开发依赖，已确认依赖完整时可设置 `SKIP_DEPS=1`：

```bash
./scripts/verify_cross_compile.sh /workspace P550
```

## 合并分析

`chroot_exec.sh`、`install_chroot_deps.sh`、`use_rootfs_sysroot.sh` 理论上可以全部合并进 `riscv_env_setup.sh`，`verify_cross_compile.sh` 也可以写成 README 中的一长串命令。本次优化选择让 `riscv_env_setup.sh` 做挂载总入口，让 `verify_cross_compile.sh` 做验证总入口，底层脚本继续保持单职责：挂载状态、rootfs 软件包状态和工具链 sysroot 状态分别可单独排查和回滚。


## make_desktop_images 中断恢复

SSH 断线或构建被杀后，先执行：

```bash
./scripts/prepare_desktop_build_env.sh recover
```

该命令只清理路径匹配当前工程的挂载和 loop 设备，不会卸载宿主其它 loop。

## 多版本 SDK

`sdk_common.sh` 统一解析 SDK 选择。默认 `SDK_VERSION=20250730`；切换 202606 Ubuntu SDK 时设置：

```bash
export SDK_VERSION=202606
```

兼容 `SDK_VERSION=20260630` 别名，脚本会按 202606 处理。

所有入口脚本都会使用同一套解析结果：`SDK_DIR`、实际 board 目录、输出目录和 SDK 布局。202606 中 `P550` 会自动映射为 `eic7700-hifive-premier-p550`。

`install_chroot_deps.sh` 默认把 Ubuntu ports 源切到 `http://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports`，并强制 apt 使用 IPv4，以规避 92 服务器访问官方 `ports.ubuntu.com` 超时的问题。需要覆盖时设置 `APT_UBUNTU_PORTS_MIRROR`。
