# algorithm `m_trackStates` 内存泄漏修复与验证记录

## 1. 结论

本轮确认并修复了 algorithm 事件模块中的两处运行期状态泄漏：

- `TargetDetectionEventDetector::m_trackStates`；
- `NestedAbsenceDetectionEventDetector::m_trackStates`。

两处实现都会为新的 `tracker_id` 插入 map 节点，但轨迹明确离区或超过离区容忍时间后只把节点内的状态对象重置为空值，没有删除 map 键。ByteTrack 的 ID 持续递增时，历史节点会在事件检测器整个生命周期内永久保留。修复前每种事件连续产生并淘汰 10,000 个 ID 后，仍分别占用约 7.84 MB 和 8.00 MB；修复后同一测试只增加 2,320 B 和 608 B，板端结果为 2,320 B 和 656 B。

最终生产代码只修改两个事件实现文件，没有修改 media_agent、推理、NPU、VPS、解码、跟踪算法、事件阈值、ABI 或系统恢复逻辑。

## 2. 根因与影响

### 2.1 原实现

两个检测器都使用 `m_trackStates[trackId]` 为新轨迹创建状态。清理阶段原来执行：

```cpp
state = TrackedTargetEventState{};
```

或：

```cpp
resetState(state, true);
```

这些操作只释放/清空节点值中的 string、deque 和标志位，`std::map<uint64_t, ...>` 的节点与历史 ID 键仍然存在。后续每帧还会遍历所有历史节点，因此问题同时造成：

- 进程常驻内存随累计唯一 track ID 增长；
- 事件清理循环的 CPU 成本随历史 ID 数量增长；
- `event_reset()` 或 detector 析构前不会回收这些节点。

这属于可由容器所有权直接证明的逻辑泄漏，不是 glibc arena 已释放但 RSS 未下降的高水位现象。

### 2.2 最小修复

修改位置：

- `eventEdge/event/src/event/target_detection_detector.cpp`；
- `eventEdge/event/src/event/nested_absence_detection_detector.cpp`。

清理循环改为 iterator 形式，在以下条件成立时直接 `m_trackStates.erase(it)`：

1. 已存在但处于非入区状态的旧节点；
2. 同一轨迹被明确观察到位于 ROI 外；
3. 轨迹漏检时间超过 `track_exit_grace_ms`。

轨迹仍处于 ROI 几何范围内时只刷新 `lastInsideTimeMs`；仍在容忍期内时保留节点。删除后同一 ID 再次入区会由原有 `operator[]` 路径创建全新状态，既有“一次入区只报警一次、离区后重新武装”的业务语义不变。

## 3. algorithm 各阶段内存所有权复核

### 3.1 推理与前后处理

- `EdgeInfer::infer` 的 `prepared_by_signature` 是单帧局部变量，返回后释放；共享前处理不会跨帧累计。
- `ModelExecutor` 请求队列由 `queue_capacity_` 限制，当前默认容量为 1，不是无界队列。
- `Eic7700Infer` 输出 DMA cache 上限为 `output_pool_size × output_tensor_count`，析构/关闭时执行 `clear_output_cache()`。
- `ModelService` 只保存 `weak_ptr<ModelExecutor>`，每次 acquire/统计都会删除 expired 项；没有永久强引用模型执行器。
- 后处理候选框、映射指针和输出结果均为单次调用局部对象。它们会造成分配器高水位或逐帧分配开销，但没有发现跨帧无界持有。
- per-device lifecycle gate/run limiter 是按 EIC7700 设备号保存的进程级对象；生产设备集合固定，不随帧、模型或 track ID 增长。

### 3.2 跟踪

- ByteTrack 会删除 deleted tracks，单轨历史 `track_list_` 受 `MAX_TRACK_LIST_SIZE=20` 限制。
- `PersonRunningEventDetector::m_trackInfoMap` 每帧执行超时清理；`VehicleReverseEventDetector::m_trackInfoMap` 同样有超时删除。
- 没有发现跟踪结果、检测输入或匹配矩阵跨帧无界累计。

### 3.3 事件

- 本轮找到的两个无界历史轨迹表已修复。
- Pose 事件的 `m_tracks` 和 `m_pairs` 都按最后观测时间删除过期项。
- 非 track 模式的类别状态以配置标签为键，规模受模型类别/事件配置限制。
- `RequestConfigCache`、detector 表和线程告警缓冲位于 event handle 内。media_agent 每流持有一个 EventPostStep；算法配置变化会重建 AlgoDetector，旧 EventPostStep 析构时调用 `event_destroy()`。键集合受当前流配置与固定 infer 线程数约束，不随视频帧或 tracker ID 增长。
- 告警 vector 在 `reset()` 后保留 capacity，可能表现为 RSS 高水位，但仍受单帧对象/告警规模限制，不是对象持续增加。

### 3.4 检测边界

ASan/LSan/UBSan 原生测试覆盖 common、event、tracker 的可执行路径；EIC7700 供应商 NPU/VPS/VDEC 库只能在板端运行，不能由 x86 sanitizer 覆盖。板端以 FD、DMA-BUF、线程数、业务吞吐和定向容器 A/B 补充验证。对供应商驱动内部资源不能仅凭应用层代码宣称完全无泄漏。

## 4. 修复前后定向 A/B

测试保持同一个 event handle，依次生成唯一 tracker ID，将目标送入一帧，再送入超过离区容忍时间的空帧。`malloc_trim(0)` 后读取 `mallinfo2().uordblks`，避免把已释放 arena 的 RSS 保留误判为仍被对象持有。

| 事件类型 | 10,000 ID 修复前增量 | 10,000 ID 修复后增量 | 修复前/后耗时 |
| --- | ---: | ---: | ---: |
| target_detection | 7,840,704 B | 2,320 B | 274 ms / 5 ms |
| nested_absence_detection | 7,996,000 B | 608 B | 266 ms / 6 ms |

EIC7700 板端使用相同 RISC-V 库和测试负载复验：

| 事件类型 | 板端 10,000 ID 增量 | 耗时 |
| --- | ---: | ---: |
| target_detection | 2,320 B | 84 ms |
| nested_absence_detection | 656 B | 90 ms |

修复后只剩日志、配置和测试框架的常量级分配；结果不再随历史 ID 数量线性增长。

## 5. 构建、部署与验证

### 5.1 构建

- 92 服务器 `cross-proj-202606-cdky` 容器原生 Release 构建通过。
- `common_config_test`、`event_api_test`、`tracker_api_test` 共 3 项全部通过。
- 另以 AddressSanitizer、LeakSanitizer 和 UndefinedBehaviorSanitizer 执行相同 3 项测试，全部通过且没有 sanitizer 报告。
- SDK 202606 RISC-V `--build-tests --install` 编译安装通过。
- 修复库 SHA-256：`2aa6a28d0b8e66c8f8b3da6f3650d0b9c4cf9c5c98cbece8f6bef0bbe20c41f5`。

用户还原时保留的 `eventEdge_save`、`eic7700Infer_save`、`tests_save` 会被 algorithm 顶层 CMake 自动识别为重复模块并产生 target 重名。本轮没有删除或移动这些用户备份，而是在容器 `/tmp/algorithm-memory-fixed.HQrCyO` 的隔离源码副本完成构建，副本排除了 `*_save` 和历史 build 目录。

### 5.2 板端部署

修复库已部署到：

- `/home/ubuntu/workspace/test/algorithm/lib/libcdky_event.so`；
- `/home/ubuntu/workspace/test/media_agent/lib/libcdky_event.so`。

部署前文件保存在：

```text
/home/ubuntu/workspace/test/backups/algorithm-track-state-leak-20260901_171539
```

其中旧 algorithm/media_agent 事件库 SHA-256 分别为 `51e548946326a4bfa7a2be5d13e5a642f85ae6027beb32b7541d8891f6fe8dfb` 和 `d63894cc50edaaf33dd4cab25128b01c03e7ed4458ea264ecac1ee3a74ec95f4`。

### 5.3 板端功能与短稳

- RISC-V `event_api_test`：PASS。
- `media_agent` PID 19595，五路真实平台任务达到 `total/suc/fail/disable=5/5/0/0`。
- 稳定窗口约 `recv/dec/pub=128.8～129.4/s`、`infer=31.8～32.8/s`；事件告警、截图和录像正常。
- 约 7 分钟时线程数 71、FD 291、DMA-BUF 约 240；`model executor inference failed`、NPU/VPS failed、`bad_alloc`、段错误计数均为 0。
- 启动/密集告警阶段 RSS 从 502,796 KiB 阶跃到 564,204 KiB，约 2 分钟保持不变后又到 573,888 KiB。这个总进程高水位仍包含截图、录像、FFmpeg、VDEC、DMA 和多线程 malloc arena；不能用它否定已经由容器持有量 A/B 证明的 track 状态修复，也不能据此宣布总进程 24 小时内存完全稳定。
- 中途 `sp00ct3`、`sp4sa1b`、`sppnecd` 三路 RTSP 源曾持续报 `open failed: Invalid data found when processing input`，任务短暂变为 `5/2/3/0`；当时剩余两路仍以约 `recv/dec/pub=50/s`、`infer=24.8/s` 工作。随后现有自动重连恢复三路输入，最终重新稳定为 `5/5/0/0`、`recv/dec/pub≈129/s`、`infer≈32/s`。全过程未出现 algorithm、NPU 或 VPS 推理失败；该现象属于上游流可用性变化，与事件状态清理补丁无关，本轮未修改拉流逻辑。

## 6. 最终边界

本轮只解决已经证实的 algorithm 逻辑泄漏，不把系统底层资源异常、手动重启前的推理停工或后续 RTSP 源失效作为本补丁根因，也没有增加 NPU watchdog、超时恢复、拉流恢复或系统服务改动。当前 `media_agent` 保持运行，供后续长稳观察；若要评价整进程 RSS，应继续按既有方案做至少 24 小时的推理/关闭截图录像对照，并单独记录 FD、DMA-BUF、线程、告警次数与匿名 arena。
