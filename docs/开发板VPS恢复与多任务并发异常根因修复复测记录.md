# 开发板 VPS 恢复与多任务并发异常根因修复复测记录

## 1. 目的与结论

本文是《开发板多任务并发资源异常排查与恢复记录》的后续修正与代码实施记录。排查日期为 2026 年 8 月 20 日，目标板为 `192.168.88.127`，程序目录为：

```text
/home/ubuntu/workspace/test/media_agent
```

本轮没有重启开发板、没有卸载驱动、没有修改板端系统文件。结论如下。

1. `pipeline_20260630` 能让失败的 `ES_VPS_Init` 恢复，并不是它更换了 VPS 动态库，也不是它执行了硬件复位；关键是官方流水线完整执行了 `SYS -> VPS -> 缓冲/IOVA` 的初始化，以及相反顺序的资源释放，补齐了旧 `media_agent` 缺失的失败清理。
2. `_VB_GetBlock errno=22` 的直接触发池不是 VDEC 图像池，而是 YOLOv8 DSP 后处理创建的 `0x200000 × 4` 输出池。旧实现每帧执行一次内核 `GetBlock/ReleaseBlock`，与 DSP/IOVA 的延迟归还竞争，最终出现 `pool used up`。
3. 五路以上 CPU/NPU 跳动由三部分叠加：输入约 225 FPS 超过当前约 85～92 FPS 的推理服务能力；RTSP 重连造成解码突发；事件录像、快照和日志造成 eMMC/JBD2 周期性停顿。它不是单一的 NPU 频率问题。
4. 长期运行后 VDEC 停止和硬件利用率归零，现场链路是：RTSP 重连反复销毁/创建 VDEC 组和池，闭源 VDEC 库延迟保留部分 DMA-BUF；同时 DSP 临时池耗尽和 eMMC 停顿放大背压，最终解码线程不再出帧。故障态下主线程还可能卡在 futex，导致 SIGINT/SIGTERM 无法完成应用级恢复。
5. 已完成五类修复：对齐官方 SYS/VPS 生命周期；DSP 输出块持久复用；显式排空 VDEC 帧引用；相同编解码参数的 RTSP 重连复用 VDEC 组；故障日志限频并让停止条件先于同步日志写入生效。

## 2. 为什么运行 pipeline 后 VPS 能恢复

### 2.1 A/B 现场证据

原 `media_agent` 的 VPS 失败日志持续出现：

```text
BIND_BuildAndSendMsg vps bind send[-1] ...
ES_VPS_SetModParam failed
ES_VPS_Init ... set mod param failed/release resource
```

2026-08-20 14:11:54 启动 `pipeline_20260630`，14:13:37 正常结束其 worker。开发板没有重启。14:13:39，仍在板上的旧 `media_agent` 即完成两个共享模型的 `EdgeInfer init success`，之后持续产生推理和事件结果。

进一步核对确认：两个工程加载的是同一套系统 `libes_vps.so.1`。pipeline 本次启动还报告 VB 信号量已经存在、`ES_VB_GetConfig` 返回异常值，因此不能把恢复解释为“pipeline 重新执行了一次干净的 `ES_VB_Init`”。真正具有区分度的是它完成了成对的 SYS/VPS/IOVA/VB 生命周期。

### 2.2 pipeline 的有效顺序

`pipeline_20260630` 的全局顺序为：

```mermaid
flowchart LR
    A[CPipeLine::Init] --> B[ES_SYS_Init]
    B --> C[PreProcElement::Init]
    C --> D[ES_VPS_Init]
    D --> E[创建预处理 VB 池和 IOVA]
    E --> F[运行流水线]
    F --> G[停止各 Element]
    G --> H[释放背景块、在途块和 IOVA]
    H --> I[销毁预处理 VB 池]
    I --> J[ES_VPS_Deinit]
    J --> K[ES_SYS_Exit]
    K --> L[ES_VB_Exit]
```

关键代码位置：

- `proj/pipeline_20260630/src/core/src/pipeline.cpp`：先调用 `ES_SYS_Init`，所有 element 完成 `Finish` 后调用 `ES_SYS_Exit/ES_VB_Exit`；
- `proj/pipeline_20260630/src/elements/preProcessElement/preProcElement.cpp`：`ES_VPS_Init`，结束时先释放背景块和预处理池，再调用 `ES_VPS_Deinit`；
- `proj/pipeline_20260630/src/core/src/pl_mem_wrap.cpp`：建池时预取 block，运行中只在用户态队列复用，销毁池时才归还内核引用。

官方 `sample_vps` 的失败分支也会进入 VPS 清理标签，即使 `ES_VPS_Init` 中途失败，仍执行 `ES_VPS_Deinit`。这是因为 Init 可能已经部分创建 bind/message 资源；返回失败不表示“什么都没创建”。

### 2.3 旧 media_agent 的缺陷

旧 `eic7700_hw_preprocess.cpp` 在 `ES_VPS_Init` 失败后只执行 `ES_SYS_Exit`，没有执行 `ES_VPS_Deinit`。其模型打开顺序还是：

```text
NPU 模型及设备缓冲 -> SYS/VPS 前处理
```

当 VPS bind 端点处于 `fd=-1` 的部分初始化状态时，上述失败分支不能清理该状态。pipeline 后续完整的 Init/Finish 恰好补上了 VPS Deinit 和资源逆序释放，所以它能在用户态恢复，而不需要重启开发板。

## 3. 代码修复

### 3.1 SYS/VPS 生命周期

修改文件：

- `proj/media-agent/third_party/algorithm/eic7700Infer/preProcess/eic7700_hw_preprocess.cpp`
- `proj/media-agent/third_party/algorithm/eic7700Infer/infer/src/yolov8_det.cpp`
- `proj/media-agent/third_party/algorithm/eic7700Infer/model/model_plugin.cpp`

调整内容：

1. 共享 VPS 状态增加 `ready`，引用计数只覆盖完整的 SYS/VPS 生命周期。
2. 顺序改为先建立 `ES_SYS_Init/ES_VPS_Init`，再打开 NPU 模型和设备缓冲。
3. 关闭时先释放模型、NPU 输出和输入缓冲，再释放 VPS/SYS。
4. `ES_VPS_Init` 失败时始终执行 `ES_VPS_Deinit + ES_SYS_Exit`，记录返回值和 `errno`；清理后只重试一次，避免无界重试掩盖真实故障。
5. pose/cls 等 typed plugin 使用相同顺序，避免仅修复 detect 路径。

### 3.2 DSP 后处理 VB 池

修改文件：

- `proj/media-agent/third_party/algorithm/eic7700Infer/postProcess/yolov8_postprocessor.h`
- `proj/media-agent/third_party/algorithm/eic7700Infer/postProcess/yolov8_postprocessor.cpp`

故障时内核日志为：

```text
vb_pool_get_block ... pool 3 used up, blkSize 0x200000, blkCnt 0x4
[ERROR-MMZ_VB]: failed to get block from pool
```

VDEC 1920×1080 图像池约为 3 MiB、6 个 block，与日志不符；旧 `DspYoloV8PostProcessor` 恰好固定创建 `2 MiB × 4` 的输出池，因此可以确定该报错来自 DSP detection-out 输出池。

旧路径每帧执行：

```text
ES_VB_GetBlock -> DSP -> Mmap -> 读取 -> Munmap -> ES_VB_ReleaseBlock
```

DSP/IOVA 对 block 的内部引用存在短暂延迟，下一帧过快申请时可能看到 4 个 block 全部仍在占用。新实现对每个共享模型只创建 1 个 output block 和 1 个 count block，在打开后获取一次，并在全局串行的 DSP 调用中持续复用，模型关闭时才执行 `ReleaseBlock/DestroyPool`。这与官方 `PL_ES_VB_*` 的所有权策略一致。

### 3.3 VDEC 帧排空与停止顺序

修改文件：

- `proj/media-agent/src/pipeline/StreamBuffer.cpp`
- `proj/media-agent/src/decoder/EsDecoder.cpp`

`StreamBuffer::stop()` 现在主动清空 `latest_frame`、包队列、缓存推理结果和待处理 frame id，确保最后一个 VDEC frame owner 在销毁组和池之前释放。

`EsDecoder` 为每组记录 `inflight` 和 `release_failures`。销毁时先停止接收，再等待最多 5 秒使 `inflight=0`，随后按如下顺序释放：

```text
DisableChn -> DetachVbPool -> DestroyGrp -> DestroyPool -> 进程级 VDEC 引用释放
```

等待超时或 `ES_VDEC_ReleaseFrame` 失败会输出明确错误，避免“接口返回后无法知道是否还有帧”的盲区。

### 3.4 RTSP 重连复用 VDEC

修改文件：

- `proj/media-agent/src/decoder/AsyncVideoDecoder.h`
- `proj/media-agent/src/decoder/AsyncVideoDecoder.cpp`
- `proj/media-agent/src/pipeline/Pipeline.cpp`

旧逻辑在 `RTSPPuller::closeStream()` 回调中无条件执行 `decoder->stop()`；下一次连接成功后，`configure()` 又创建新 VDEC 组和 VB 池。现场首轮修复版在多次流错误后，`/proc/esmap/dec` 中 PicPoolId 反复变化，DMA-BUF 对象从约 207 增加到 224。所有单次析构都报告 `drained=1`，说明增长不是仍有外层 `shared_ptr`，而是 VDEC SDK 的进程级延迟释放行为。

新逻辑增加 `prepareForReconnect()`：

- 网络断开时只清空编码包队列和 decode ticket，等待下一个 IDR；
- 新连接的 codec、time base、FPS、bitrate、宽高和 extradata 完全一致时复用现有 VDEC 组；
- 任一编解码契约变化或 decoder 已进入 fatal 状态时，仍执行完整销毁和重建。

这一区分了“网络连接生命周期”和“硬件解码器生命周期”，避免短暂网络抖动演变为 MMZ 池抖动。

### 3.5 日志风暴与退出响应

修改文件：

- `proj/media-agent/src/decoder/AsyncVideoDecoder.cpp`
- `proj/media-agent/src/stream/RTSPPuller.cpp`
- `proj/media-agent/src/pipeline/Pipeline.cpp`
- `proj/media-agent/src/main.cpp`

故障态中每路 `input queue full` 和 `av_read_frame interval exceeded` 都以 WARN 输出，而 Logger 对 WARN 配置了同步 `flush_on`。当 eMMC/JBD2 已经拥塞时，这会同时增加写放大和日志 sink 互斥锁等待。

修复后：

- 每路 decoder 的 queue-full WARN 最多 5 秒一次，并在一条日志中汇总区间丢包数；
- 每路 RTSP interval WARN 最多 5 秒一次；
- `Pipeline::stop()` 先设置 stop flag、唤醒配置/推理线程，再输出 stopping 日志；
- main 收到信号后先调用 `pipeline.stop()`，完成后再记录 shutdown 日志，避免退出动作被第一条同步日志阻塞。

## 4. 三个现场问题的根因

### 4.1 `_VB_GetBlock` 和推理失败

根因是 DSP 后处理输出池的所有权策略错误，不是模型包随机损坏，也不是 VDEC 输入参数偶发变为非法。内核的 block size/count 与 DSP 池一一对应；池耗尽后 `ES_AK_DSP_DetectionOut` 无法获得输出 block，外层统一记录为 `EdgeInfer infer failed ret=-1`。

修复后的首轮 40 个采样点中：

```text
VB_GetBlock 错误       0
EdgeInfer infer failed 0
VPS init failed        0
VDEC release failed    0
VDEC drain timeout     0
```

### 4.2 五路以上 CPU/NPU 利用率跳动

当前配置默认只有 3 个推理线程；同一模型由共享 `ModelExecutor` 串行执行，默认请求队列容量为 1。现场 9 路有效输入约 225 FPS，而稳定推理总吞吐约 85～92 FPS。Round-robin scheduler 保证每流最多一个在途任务并选择最新帧，因此过载时必然表现为丢旧帧和脉冲式运行，而不是 9 路各自稳定 25 FPS 推理。

首轮修复版典型数据：

```text
recv/dec/publish 约 225 FPS
infer            约 88～92 FPS
进程 CPU         约 94%～125%
NPU              约 85%～92%（活跃采样）
DSP0             约 84%～86%（活跃采样）
VDEC             约 35%～37%，约 244～247 FPS（含积压突发）
```

此外，事件压力测试会产生大量告警输出。现场 10 分钟内事件目录新增约 973 个文件、1.85 GiB，包含录像片段、快照和临时文件。eMMC 的数据写入与目录项、inode、journal 更新会让所有 RTSP/推流线程同时停顿 1～3 秒；停顿后积压帧集中进入 VDEC，又形成瞬时高 FPS。CPU/NPU 曲线因此不能只按某一个 `es_hw_watcher` 窗口解释。

### 4.3 VDEC 异常、硬件利用率为 0 和 D 状态

已抓到 `media_agent` D 状态线程的内核栈：

```text
do_get_write_access
  jbd2_journal_get_write_access
    __ext4_mark_inode_dirty / ext4_create
      ext4_file_write_iter / openat
        riscv_sys_write / riscv_sys_openat
```

同一时刻还有：

```text
jbd2/mmcblk0p3-8      wait_on_buffer
flush-179:0           do_get_write_access
kblockd               mmc_blk_rw_wait
systemd-journal       ext4_truncate
平台 event poller     do_get_write_access
```

因此“大量 D 状态”的主因是 eMMC/ext4/JBD2 写回和元数据争用。它会暂时阻塞算法日志、录像和快照线程，但不是 media_agent 永久卡在 VDEC/NPU ioctl 的证据。

`es_hw_watcher` 自身也有采样窗口：同一次输出的第一个窗口可能显示 NPU/VDEC 为 0，紧接着第二个窗口显示 NPU 85%、VDEC 245 FPS。只有同时满足 `/proc/esmap/dec` 计数长期不增长、应用统计 `dec/infer=0`、多个连续 watcher 窗口为 0，才能判定硬件流水线真的停住。

本轮确实再次捕获到一个真实冻结态：9 路输入仍约 200 FPS，但 `dec=0、infer=0、publish=0`，所有输入队列持续满载；SIGINT 和 SIGTERM 已投递且应用注册了 handler，主线程却长期等待 futex，未打印 shutdown 日志。该进程只能在保存诊断文件后 SIGKILL，退出后再重新初始化。这说明第三点不是纯显示误差，而是“资源/重连长期积累后 VDEC 停止 + 主线程响应路径阻塞”的复合故障。

## 5. 编译、部署与验证

### 5.1 交叉编译

92 服务器直接宿主构建的旧 CMake cache 包含 `/workspace`，宿主又缺少 `/opt/riscv`，因此最终在既有 SDK 容器中编译：

```bash
docker exec cross-proj-202606-cdky bash -lc '
  cd /workspace/proj/media-agent &&
  ./scripts/build.sh --target riscv64 --jobs 16 --install
'
```

最终 VPS/VB/重连/日志修复版产物：

```text
SHA256  f57d55188202f2ad761a7a82f02b53dee2eb161d5808b4fc7a8d8d1a41127ba3
ELF     64-bit RISC-V, RVC, LP64D
```

构建日志：

```text
容器内 /tmp/media_agent_build_final_vps_vb_reconnect_log_20260820.log
```

### 5.2 板端备份与运行目录

首轮 VPS/VB 修复备份：

```text
/home/ubuntu/workspace/test/backups/media_agent-vps-vb-fix-20260820_144200
```

最终重连修复版替换前备份：

```text
/home/ubuntu/workspace/test/backups/media_agent-reconnect-reuse-20260820_150647
```

最终日志限频版替换前备份：

```text
/home/ubuntu/workspace/test/backups/media_agent-final-log-throttle-20260820_151441
```

首轮 40 点监控目录：

```text
/home/ubuntu/workspace/test/media_agent/diagnostics/vps_vb_fix_20260820_144229
```

不运行 pipeline 的独立重启目录：

```text
/home/ubuntu/workspace/test/media_agent/diagnostics/vps_vb_restart_20260820_145430
```

最终重连修复版目录：

```text
/home/ubuntu/workspace/test/media_agent/diagnostics/vps_vb_reconnect_reuse_20260820_150649
```

当前最终版运行目录：

```text
/home/ubuntu/workspace/test/media_agent/diagnostics/final_vps_vb_reconnect_log_20260820_151443
```

### 5.3 独立重启证明

首轮修复版运行约 11 分钟后发送 SIGINT：

- 所有被销毁 VDEC 组均记录 `inflight=0 release_failures=0 drained=1`；
- 进程约 3 秒退出；
- DMA-BUF 回到 2 个系统基线对象；
- 没有运行 pipeline，直接再次执行 `sudo ./media_agent`；
- 9 个 VDEC 组全部进入 START，推理恢复约 90 FPS；
- watcher 显示 NPU、DSP、VDEC、HAE 均有负载。

这证明修复后的 `media_agent` 已能自行完成正常情况下的 VPS/SYS/VB 生命周期，不再依赖 pipeline 充当清理器。

### 5.4 重连复用实测

重连修复版在约 6 分钟窗口内经历两轮多路同时 RTSP 断线，共记录 20 次：

```text
[AsyncVideoDecoder] stream=... reuse decoder after reconnect ...
```

复用期间没有出现 `teardown release guard`，说明没有销毁 VDEC 组；DMA-BUF 始终在 207～209 个对象、约 586.5～588.1 MiB 之间波动，没有像旧路径一样随这 20 次重连阶梯增长。窗口结束后 SIGTERM 约 4 秒完成退出。

最终版已于 15:14:43 再次启动，PID 为 `416964`。初始状态为 10 个任务、9 路成功、1 路输入数据无效；有效流约 225 FPS 解码/转发、约 86.8 FPS 推理，未出现 VPS、VB、推理或 VDEC release 错误。程序在交付时保持运行。

## 6. 尚需持续验证与生产建议

1. 重连复用版需要在平台真实网络抖动下至少运行 12～24 小时，观察 `reuse decoder after reconnect` 次数与 DMA-BUF 对象/字节是否保持平台期；不能用数分钟结果宣称长期泄漏完全消失。
2. 为压力测试增加“只推理不录像/快照”的模式，与完整事件落盘模式分开测量。当前 61 GiB 事件目录和高频小文件会显著污染硬件吞吐结论。
3. 对告警事件进行合并、冷却或最大并发录像限制；算法日志、interval warning、queue-full warning 应按流限频。
4. 增加 watchdog：当连续多个周期 `recv>0 && dec=0` 时抓取 `/proc/esmap/dec`、DMA-BUF、线程栈并触发受控重启；若 SIGTERM 超时，再升级 SIGKILL。
5. 根据目标推理 FPS 做准入控制。9 路 25 FPS 输入与约 90 FPS 推理能力不匹配时，应明确配置每路目标推理 FPS，而不是依赖拥塞后的偶然丢帧。
6. `es_hw_watcher` 应至少读取两个连续窗口，并与应用统计和 `/proc/esmap/dec` 增量联合判断。
