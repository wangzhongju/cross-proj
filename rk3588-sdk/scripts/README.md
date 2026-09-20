# RK3588 交叉编译脚本

本目录只保留创建和使用 aarch64 交叉编译环境所需的四个入口。容器统一由根目录 `docker/docker.sh` 管理；sysroot 的镜像挂载与 chroot 扩展过程记录在根目录 `run.md`。

| 脚本 | 作用 |
| --- | --- |
| `setup-toolchain.sh` | 检查瑞芯微 SDK GCC 10.3.1，并在 `rk3588-sdk/compat-bin/` 建立 `aarch64-linux-gnu-*` 到官方 `aarch64-none-linux-gnu-*` 的兼容链接。`docker/docker.sh start` 会自动执行。 |
| `board-protoc.sh` | 在 x86_64 容器中通过 `qemu-aarch64-static` 运行 aarch64 sysroot 内的目标版本 `protoc`，避免宿主 Protobuf 版本与目标板不一致。 |
| `run-media-agent-build.sh` | 在统一容器内以 aarch64、默认 8 并发执行 `media-agent` 编译和 `--install`，日志写入 `rk3588-sdk/artifacts/media-agent-build-aarch64.log`。 |
| `run-media-agent-package.sh` | 在统一容器内以 aarch64、默认 8 并发生成可选 `.deb`，日志写入 `rk3588-sdk/artifacts/media-agent-package-aarch64.log`；不会在目标板安装。 |

推荐从宿主机项目根目录调用：

```bash
./docker/docker.sh start
./docker/docker.sh init '/workspace/rk3588-sdk/scripts/run-media-agent-build.sh 8'
./docker/docker.sh init '/workspace/rk3588-sdk/scripts/run-media-agent-package.sh 8'
```

