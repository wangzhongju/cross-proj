# cross-proj 更新日志

本文件统一记录 `cross-proj` 及 `proj` 下各工程的功能变更、问题修复、构建验证和板端验证状态。

- 日期按从新到旧排列，每个日期只使用一个二级标题。
- 同一日期的不同事项使用编号三级标题；事项内部统一使用四级标题描述根因、修改和验证。
- 同一问题存在多轮定位时按阶段连续记录，并明确后续方案是否取代前一方案；不再创建重复日期标题。
- 每项记录应覆盖所有受影响工程、最终采用方案、构建环境、部署位置及验证结论，避免只在子工程保留零散说明。

## 2026-09-14

### 1. Algorithm 测试参数核对与官方 YOLOv5s 基线入口恢复

#### Algorithm 参数结论

- 核对 `algorithm/eic7700Infer/tests/eic7700_stream_infer_test.cpp` 与 127 板端已部署程序的 `--help`：工具支持图片、图片目录、本地视频和 RTSP，参数覆盖模型选择、设备/VDEC、阈值、固定时长压测、仅预处理和硬件可视化输出。
- 当前源码的图片与流路径都会在 `EdgeInfer::init` 前解包 pkg、解析运行配置并写入临时覆盖配置，因此 `--iou-thresh` 和 `--topk` 在视频、RTSP 与图片模式下都会被接受并写入配置。实际消费取决于后处理插件：`--iou-thresh` 在 YOLOv8 DSP/CPU、YOLOv5、YOLOv26 和 Pose 后处理中生效；`--topk` 在 YOLOv8 CPU、YOLOv5、YOLOv26、Pose 和分类后处理中生效，但当前 YOLOv8 DSP DetectionOut 只读取 confidence/IoU 与 DSP 最大框数，未读取 `config.topk`，所以该组合下 `--topk` 实际不生效。`--confidence` 同时覆盖模型后处理阈值和最终结果过滤阈值。
- 127 板端 `/home/ubuntu/workspace/test/algorithm/bin/eic7700_stream_infer_test` 已显示上述参数，SHA-256 为 `841f7013de9274623996f34dd43fa7a174116d38c423c6a4d29635e63cb88d7b`。

#### Pipeline 测试入口修正

- `pipeline_20260630/case/run.md` 的无显示 infer/OD 示例与 `case/es_run_model/README.md` 的纯模型示例不再引用已删除的 `coco_detect_cls80_yolov8s_512_eic7700.model`，统一改用工程随附的 `models/yolov5s_git_1x3x416x416.model`。
- `case/od/od_headless_pressure.sh` 现在核对官方模型 SHA-256 `68ffdf69...`，匹配后自动使用 6,943,179,776 OPS/帧的稠密卷积 MAC=2 口径，并在 `summary.json` 保存模型绝对路径和哈希；非官方模型必须显式提供配套运行时 JSON，避免误用 YOLOv5s 的 416×416、BGR 和 DetectionOut 参数。
- `case/es_run_model/run_benchmark.sh` 新增官方 YOLOv5s 纯模型一键压测：执行 NPU 独占与模型哈希检查、短测、预热、正式计时和可选真实输入/输出张量核对，保存原始性能 JSON、环境与 NPU 状态并生成统一 `summary.json`；`case/run.md` 和目录 README 已补充使用方法。127 板端以工具生成输入完成 20 次短测、100 次预热和 1000 次正式计时，min/max/avg 为 4.2630/4.6660/4.2866 ms，233.2866 FPS，约 1.6198 等效 TOPS；NPU 完成计数增量 1140，与短测及正式阶段含预热的总任务数完全一致。
- 127 板端同名官方模型哈希核对一致；最终压测在无其他 NPU 进程、计数稳定的窗口执行，结果保留于板端 `/home/ubuntu/workspace/test/pipeline_20260630/result_output/es_run_model_yolov5s_416_20260914_verified/`，并同步至本地 `outputs/diagnostics/es-run-model-yolov5s-20260914/`。临时上传到 `/tmp` 的脚本已删除，业务目录未被覆盖；历史官方模型基线证据仍位于 `outputs/diagnostics/official-od-20260910/`。

### 2. Pipeline 25 路无显示 OD 的 ERROR 修复与退出问题暂缓

#### ERROR 现象、根因与已保留修复

- 127 板端执行官方 YOLOv5s 25 路无显示 OD 压测后，原结果目录
  `result_output/yolov5s_official_od_25ch` 的 `pipeline.log` 达 565 MB，应用层约有
  2,804,447 条 ERROR。其中 `ES_VDEC_SendStream failed`、上层音频/视频
  `ProcessAndTransmit error` 各约 934,815 条，19 个 VDEC 组重复报错。
- SDK 日志首先出现 `_VB_GetBlock errno 24`、`dmabuf_heap_alloc_fdflags errno=24` 和
  `SYS_MemAlloc ... Too many open files`；`0xa0036014` 按 SDK 头文件解码为
  `VDEC_ERR_FATAL=20`。板端普通用户和 `sudo` 进程的 `RLIMIT_NOFILE` 软限制均为 1024，
  25 路 VDEC 的设备 FD、DMA-BUF 和 Pipeline FD 超过该上限，这是大量 ERROR 的直接原因。
- `case/od/od_headless_pressure.sh` 现默认把进程 `nofile` 软限制提高到 65536，并按
  `1024 + streams * 128` 计算最低需求；无法达到最低值时在启动前失败。脚本同时修复长进程名
  检查，运行期定时扫描 Pipeline ERROR，发现错误立即保存 `detected_errors.txt` 并终止；
  `summary.json` 增加软/硬限制、ERROR 总数、VDEC 送流错误和 FD 耗尽错误字段。
- 提高限制后的 25 路短测完成 1501 次推理，约 220.09 FPS、NPU busy 93.38%；
  `pipeline.log` 约 7.1 KB，ERROR、`ES_VDEC_SendStream failed` 和
  `Too many open files` 均为 0。该结果证明本次用户反馈的大量 ERROR 已由 FD 限制修复。

#### 尚未解决的正常退出问题

- ERROR 消失后暴露出独立的停止阶段问题：25 路进程收到 SIGINT 后，30 秒内不能正常退出；
  将等待时间临时扩展至 80 秒仍不能退出，脚本最终发送 SIGKILL。1 路相同链路可以正常退出，
  因此该问题与 25 路并发下的 VDEC 停止路径相关，而不是模型、DetectionOut 或 FD 耗尽。
- GDB 确认主线程阻塞于 `pthread_join -> VdecElement::Wait ->
  CPipeLine::WaitForFinish`；25 个 `decoderN_get_frame` 线程均停在厂商
  `/lib/libes_vdec.so.1` 的 `ES_VDEC_GetFrame` 条件等待中。Demux 送流线程已退出，
  Queue/Infer 线程仍在等待下游 EOS。测试阶段没有 Pipeline ERROR，卡住发生在测量结束后的收尾阶段。
- 已尝试但未解决的方法包括：把停止等待从 30 秒延长到 80 秒；在取帧循环检查
  `PIPELINE_STOPPING`；调整 `StopRecvStream` 与 get-frame join 的先后顺序；销毁前有界排空并
  `ES_VDEC_ResetGrp`；把 `GetFrame` 等待参数分别改为 0 和 1；移除无显示链路未消费的 PP1；
  使用普通及异步 `pthread_cancel`；通过 GDB 单独调用 `ES_VDEC_ResetGrp`、
  `ES_VDEC_CloseFd` 和 `ES_VDEC_Deinit`。Reset/CloseFd 可返回成功但不能可靠唤醒取帧线程，
  Deinit 返回错误，线程取消在厂商调用内不生效。
- 按本次决定，正常退出问题暂缓，不再继续修改通用 VDEC 生命周期代码。后续若重启处理，
  应优先向 SDK 厂商确认 `ES_VDEC_GetFrame` 的超时/取消语义及 25 路推荐停机顺序，并以
  “25 路、无 ERROR、SIGINT 后 30 秒内退出、无残留 VDEC 组”为验收条件。

#### 回滚与恢复状态

- 所有为正常退出问题做的实验性修改均已撤销：本地和 92 服务器
  `src/elements/vdecElement/vdecElement.cpp` 恢复为原始版本，SHA-256 为
  `cbfc205e81e685bcf206ca62d2323944402a2b0dfba1064fd4a5e0e18aed5801`；92 在
  SDK_VERSION=202606 容器重新交叉编译安装成功，恢复后的 `libes_plvdec.so.1.0` SHA-256 为
  `3568f3f1191b69b75494d0b893ceecdf3cde1c7c804f47477fb51f64f1f71b94`。
- 127 板端已从部署前备份恢复同一 SHA-256 的 VDEC 库；为退出问题临时加入的 PP1 删除和
  动态 80 秒等待也已从压测脚本撤销。板端当前没有残留 `espl_launch` 或压测脚本进程。
- FD 限制、启动前资源校验、运行期 ERROR 快速失败和结果统计属于原始 ERROR 问题的正式修复，
  不在本次回滚范围内。

### 3. vehicle-parking 告警目标与按轨迹单次报警修复

#### 根因

- `vehicle_parking` 触发时只填写事件名称、描述和扩展字段，没有把当前命中车辆写入
  `EventInfo.objects`，因此 Media Agent 生成的告警缺少事件目标；`area-intrusion` 使用的
  `target_detection` 路径则会返回对应目标。
- `VehicleParkingConfig` 和工厂解析此前均未定义 `track_id_filter_enabled`，所以即使在
  `Event.yaml` 为 `vehicle-parking` 添加该字段也会被忽略。原实现还只维护一份全局在区状态，
  无法按车辆轨迹分别计时、锁存和重新布防。

#### 修改

- `vehicle_parking` 普通模式触发时返回当前命中车辆，保留其类别、置信度、边界框和
  `tracker_id`，告警目标行为与 `area-intrusion` 对齐。
- 新增 `track_id_filter_enabled` 和 `track_exit_grace_ms` 配置解析。开启过滤后，每个有效
  `tracker_id` 独立累计 `parking_time_threshold`，一次连续入区仅报警一次；明确看到同 ID
  离开 ROI 时立即重新布防，同 ID 再进入并重新满足停车时长后可再次报警。
- 区内低置信度帧不会被误判为离区；轨迹完全缺失超过 `track_exit_grace_ms`（默认 2000 ms）
  才重新布防，避免短时漏跟踪造成重复报警。过滤模式下没有有效 `tracker_id` 的车辆不参与判定。
- `algorithm/config/Event.yaml` 与 `vision-pipeline/server/config/Event.yaml` 的
  `vehicle-parking` 均启用 `track_id_filter_enabled: true`，并配置 2000 ms 离区宽限；中英文
  EventEdge README 同步补充语义说明。
- `event_api_test` 新增告警目标字段断言，以及同 ID 连续在区不重复报警、不同 ID 独立报警、
  低置信度恢复不重复报警、离区重入重新报警的覆盖。

#### 验证

- 92 服务器 `cross-proj-202606-cdky` 容器中，Algorithm 执行
  `./scripts/build.sh --target riscv64 --jobs 16 --install --build-tests` 交叉编译安装通过。
- 同一容器执行原生 `./scripts/build.sh --target native --jobs 16 --build-tests`，随后
  `ctest --test-dir build --output-on-failure`，`common_config_test`、`event_api_test`、
  `tracker_api_test` 共 3 项全部通过。
- Media Agent 执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 集成交叉编译安装通过，
  新版 `libcdky_event.so`、`Event.yaml` 和 `media_agent` 已生成于 92 服务器对应安装目录。
- 91 服务器 `/home/cdky/workspace/gitlab/vision-pipeline/server/config/Event.yaml` 已同步相同的
  `vehicle-parking` 跟踪过滤配置并通过 `git diff --check`；本次未重新量化或打包模型。
- 92 生成的 RISC-V `event_api_test` 与 `libcdky_event.so` 使用临时目录上传到 127 板端执行，
  输出 `[event_api_test] PASS`；测试后已删除板端及本地临时文件。
- 本次未覆盖 127 板端现有部署，也未下发真实 `vehicle-parking` 平台任务；生产部署与真实视频
  验证状态保持不变。

## 2026-09-11

### 1. YOLOv8s 640 INT8 量化、区域入侵发布与 EIC7700 多路径算力实测

#### 模型、量化与精度

- 使用 Ultralytics 标准 YOLOv8s COCO 80 类权重，显式导出为 640×640、batch=1；源 PT SHA-256 为 `1f47a78b...`。按既有 `pt_to_eic7700_deployment.md` 流程，以 200 张 train 图片校准、20 张分析，全局 INT8、三个输出头 INT16，EsAAC 编译成功；未新增量化脚本。
- 编译模型 SHA-256 为 `2af2c4f2...`，稠密卷积按 MAC=2 OPS 为 28.602624 GOP/帧。项目冻结的 200 张 COCO val 全量评估中，PT/板端 mAP50:95 为 46.707%/46.527%，下降 0.181 个百分点；mAP50 为 63.716%/63.886%。完整 AP/AR、类别明细与一致性结果见 `docs/EIC7700边缘设备npu计算能力实测.md`。

#### 发布与板端结果

- 新增独立清单 `vision-pipeline/configs/eic7700_yolov8s_int8_benchmark.json`，91 发布模型级 `coco_detect_eic7700_1_6.pkg` 和区域入侵 `area-intrusion_eic7700_1_6.pkg`。事件包只公开 person/car，SHA-256 为 `05c238d3...`；已复制到 127 的 `/home/ubuntu/workspace/test/models/eic7700_platform/area-intrusion/`，保留原 `1_5`。
- 127 实测：`es_run_model` 78.232 FPS/2.238 等效 TOPS；Algorithm 纯 NPU 76.779 FPS，含 DMA 预处理与 DSP 后处理 54.762 FPS；官方基线 Pipeline 无显示 infer/OD 为 78.635/78.575 FPS；Media Agent 单路为 24.995 FPS，三路饱和为 75.089 FPS/2.148 等效 TOPS、NPU busy 96.620%；官方历史版本 Pipeline 的十路业务链路为 75.862 FPS，比官方基线 OD 低 3.45%。13.3 TOPS 对比采用当前模型卷积等效口径，不代表芯片所有算子的物理利用率。
- 更正工程归属：`pipeline` 是 `pipeline_20260630` 的官方历史版本，并叠加了与 Media Agent 大致相同的业务代码，不再称为“自研 Pipeline”。其十路运行阶段 NPU 99.551%、无 critical error，但停止阶段 45 秒内仍有 8 个 worker，脚本按设计报失败，随后进程全部退出；该吞吐可用于性能比较，退出超时作为稳定性问题保留。

#### Algorithm 与 Media Agent 性能定位

- Algorithm `npu` 只计已经预处理好的输入张量进入 runtime 的循环；`pipeline` 每帧同步包含 VPS/DMA 预处理、NPU、DSP DetectionOut 和结果过滤。两模式的驱动 NPU busy/次分别为 12.6882/12.6892 ms，54.762 FPS 的根因是约 5.236 ms 同步前后处理空窗，不是 NPU 推理变慢。200 图分段均值为预处理 2.7998 ms、NPU 13.1338 ms、后处理 3.8616 ms。
- 官方 Pipeline `infer` 包含解码和预处理，但不含 EsPostProcess；`od` 在其后增加 DSP DetectionOut。异步 Queue 隐藏后处理，二者只差 0.061 FPS（0.077%）；纯模型基线仍以 `es_run_model` 为准。
- Media Agent 原先按 pkg SHA-256 将同模型合并到唯一单 worker `ModelExecutor`，多流仍只能约 52.5 FPS、NPU busy 66.8%。`ModelService` 现按 `npu_max_inflight` 建立同模型 executor 池，配置为 3 以匹配三个推理线程；模拟任务服务新增 `--events` 与 `-s` 联用的同源多流模式，并把区域入侵映射统一到 `area-intrusion_eic7700_1_6.pkg`。三路 100 FPS 输入的 60.008 秒实测完成 4506 次推理，稳态业务 infer 74.6～75.2 FPS，无推理失败。
- 92 服务器 SDK 202606 容器内重新执行 Algorithm 和 Media Agent 的标准 riscv64 install 构建均成功；板端部署前的 Media Agent、推理库和 EsInfer 配置已备份到 `/home/ubuntu/workspace/test/backups/media_agent_before_executor_pool_20260911/`。

#### Pipeline 用例与证据

- `pipeline_20260630/case/od/od_headless_pressure.sh` 新增无显示 infer/OD 模式、NPU 独占检查、运行时配置适配、硬件采样、哈希和 JSON 汇总；修复官方 PostProcess 配置含两个 `labelfile-path` 时的适配。`case/run.md` 已补充命令。
- 新增 `pipeline_20260630/case/es_run_model/README.md`，记录模型核对、预热、1000 次计时、真实输入校验和结果口径；按板端实测记录 `-s` 固定生成 `model_perf_data.json` 的行为。
- 92 SDK 202606 容器内 Algorithm（含测试）和 `pipeline_20260630` 交叉构建安装均成功；本地原始证据保存于 `outputs/diagnostics/yolov8s-int8-20260911/`。

## 2026-09-09

### 1. 92 服务器重启后 SDK 202606 交叉编译失败恢复

#### 现象与根因

- 在 `cross-proj-202606-cdky` 容器的 `/workspace/proj/media-agent` 原样执行 `./scripts/build.sh --target riscv64 --jobs 16 --install`，CMake 的 C 编译器检查失败；实际错误是链接器 `cannot find crt1.o: No such file or directory`，尚未进入业务源码编译。
- 92 宿主机启动时间为 `2026-09-09 16:06:05 CST`，容器启动时间为当天 `16:12:32 CST`。排查时 SDK rootfs 未挂载、挂载目录为空，也没有该 SDK 镜像的 loop 映射；`/opt/riscv/sysroot` 仍保留指向 `/workspace/eswin-sdk-202606-ubuntu/eic7700-hifive-premier-p550/output/rootfs` 的软链接。重启后软链接保留，但镜像挂载没有恢复，导致 C 运行时启动文件和目标库不可见。
- `docker/docker.sh` 的 `start/init` 调用 `prepare_desktop_build_env.sh setup`，该步骤只准备 QEMU binfmt 和 loop 节点，不执行 rootfs 挂载；业务工程的 `scripts/build.sh` 则直接使用现有 sysroot。这一启动流程缺口造成此次错误。

#### 恢复方法与变更范围

- 在 SDK 202606 容器内执行以下命令，复用现有镜像和依赖，无需重建 Docker 镜像、重新安装 libc 开发包或修改业务代码：

```bash
cd /workspace
export SDK_VERSION=202606
./scripts/riscv_env_setup.sh /workspace P550 setup
test -f /opt/riscv/sysroot/usr/lib/riscv64-linux-gnu/crt1.o

cd /workspace/proj/media-agent
./scripts/build.sh --target riscv64 --jobs 16 --install
```

- 已在 92 服务器完成恢复；镜像根分区当次挂载为 `/dev/loop26p3`，`crt1.o` 恢复可见。loop 编号由系统分配，后续使用脚本自动查找，不应固定该编号。
- 服务器或容器重启、重建，以及执行挂载清理之后，进入容器应先运行上述 `setup`。本次重复执行验证为 `already mounted`，可以作为日常编译前置步骤。仅设置 `SDK_VERSION`、查看软链接或执行 `source_sdk_env.sh` 不能代替挂载。
- 本次只恢复编译环境挂载、重新构建安装并补充文档，未修改任何工程源码或构建脚本；保留本地及服务器已有未提交修改。没有执行依赖更新、镜像重建或板端部署。

#### 验证与日志

- `media-agent`、`media-agent/third_party/algorithm`、`pipeline`、`pipeline_20260630` 均在同一容器内使用原命令 `./scripts/build.sh --target riscv64 --jobs 16 --install` 成功完成构建与安装，退出码均为 0。media-agent 从失败的配置阶段恢复并完成源码编译；其余工程为现有构建目录上的增量验证，未执行 `--clean`。
- 安装产物位于各工程 `build-riscv64/install`；`file` 确认 `media_agent`、`libcdky_eic7700_infer.so` 和两个 pipeline 的 `espl_launch` 均为 RISC-V ELF。
- 恢复挂载后原 16 并发命令直接成功，证明此次故障无需通过降低并发解决。media-agent 当前脚本实际默认构建类型为 `RelWithDebInfo`，与帮助文本及历史日志中的 Release 描述不一致；此次按原命令验证，未调整优化策略。
- 失败日志及四个工程的成功日志保存到本地和 92 服务器项目的 `outputs/diagnostics/sysroot-20260909/`。本次验证范围是交叉编译与安装，未连接 91/127/57，也未执行板端业务或模型推理测试。

### 2. EIC7700 单路 SCJJ 实际算力与 NPU 63% 占用率分析

#### 现场核对与量化口径

- 127 板端实际单路推理约 25 FPS，安全帽和反光衣共享同一个 SCJJ 执行器；不是同帧重复执行两遍模型。NPU 在最高可用频率 1040 MHz 工作，温度约 49°C。
- 91 量化日志确认当前 SCJJ YOLOv8s 640×640 五类模型为 `dtype=int16 nodes_i8=/model.0/conv/Conv`，以 INT16 为主；官方 INT8 13.3 TOPS 不能直接作为这一模型的同精度基准，官方 INT16 峰值为 6.65 TOPS。板端模型与 91 编译模型 SHA-256 均为 `2dd90c9991a88b30ea93b4fcd57642c6c0ed5b4a49526e96fdd44664d398aecf`。
- 编译图包含 95 个卷积节点（4 个首层 INT8 切片、91 个 INT16）、76 个 DSP0 `lut_act`、144 个 EDMA、176 个 SDP 等。模型任务时间包含多引擎和调度影响，监控占用率不能换算成 MAC 阵列效率。

#### 采样结果与结论边界

- `es_hw_watcher -i 2 -c 5` 稳态 NPU 占用为 63.47%～63.98%。独立读取 `/proc/esnpu/stat`：20.0091 秒完成 500 次任务，24.9886 次/秒，忙时占比 63.5744%，平均 25.4414 ms/次。
- 使用真实模型导出日志中的 28.4 GFLOPs，当前模型等效吞吐约 0.710 TOPS；忙时线性归一化约 39.3 次/秒、1.12 TOPS。后两者是当前模型与软件方案的外推结果，尚非饱和压测，更不是芯片物理峰值测量。
- 当前一路 25 FPS 已验证；两路各 25 次/秒按现有耗时会超过容量。建议后续分别验证逐算子耗时、INT8/混合精度及准确率、激活/搬运融合，再做离线饱和和多路容量测试。
- 完整分析见 `docs/EIC7700边缘设备npu计算能力实测.md#scjj-yolov8s`；证据保存在 `outputs/diagnostics/npu-scjj-20260909/`。本次没有修改源码、业务配置或模型，没有重启业务进程，也没有执行竞争性推理压测。

### 3. YOLOv5s INT8 全量精度评估、区域入侵发布与 NPU 算力实测

#### 模型、量化与算法适配

- 91 下载 Ultralytics 官方 v7.0 `yolov5s.pt` 至 `weights/train/yolov5s_det`，固定源码 commit `915bbf294bb74c859f0b41f1c23bc395014ea679`；模型为经典 YOLOv5s、COCO 80 类、640×640、batch=1。
- vision-pipeline 新增 `tools/yolov5_eic7700.py`、COCO JSONL 评估及一致性/操作量验证工具；扩展 `eic7700_pipeline.py` 支持 `yolov5_det` 三头 INT8 ABI、anchors 元数据、CPU 后处理配置和输出精度校验。固定倍率 Resize 替换动态 sizes，解决 EsQuant 的 NoneType 图融合错误；补齐无 ONNX Python 依赖时的 YOLOv5 输出名，避免错误套用 YOLOv8 输出名。
- 使用 COCO train2017 的 200 张图片进行 INT8 校准、20 张进行逐层余弦分析；val2017 的 5,000 张全部保留用于独立验证。编译图全部卷积输入/权重为 INT8，包含 67 个卷积节点、50 个 DSP 激活节点。EsSimulator 不在镜像中，仿真被跳过，真实板端验证另行完成。
- algorithm 新增 `yolov5_det` 模型插件，支持 anchors、obj×class、三头 stride/量化尺度解码、class-aware NMS 和原图坐标还原；现有流测试增加固定帧 `npu`/`pipeline` 压测模式。新增 `scripts/eic7700_yolov5_pressure.py` 收集重复/持续测试、watcher、频率、温度和驱动计数。

#### 构建与精度验证

- 92 在 SDK 202606 容器交叉构建测试程序。首次增量产物在完整算法初始化/退出中出现堆释放异常；完整重建并明确 sysroot 依赖路径后，RelWithDebInfo/Release 的 20 张试跑均通过，正式 Release 完成 5,000 张、5,000 次推理并正常退出。最初内存异常的精确触发机制未由现有证据完全确定；ASan 自身初始化异常，未作为通过项。
- 修复前后 ONNX 三组随机输入输出差异为 0；50 张真实图片 PT/ONNX 对比最大框误差 0.007385 像素，最大概率误差 8.5533e-6。
- COCO 全量 FP32 AP50:95=37.3948%、INT8 板端=35.0471%，下降 2.3478 个百分点；AP50 从 56.1586% 降为 54.3670%，下降 1.7916 个百分点。保留原始 crowd/area 标注，另提供 80 类明细、区域入侵类别及工作阈值 P/R/F1。部署损失包含 NV12 与硬件预处理差异，不全部归因于量化。
- 打包现有 CPU 测试 8 项通过、1 项真实 SCJJ 可选测试跳过；新事件包解密核对 class_id=0/2、num_class=80、anchors 和模型哈希均正确。

#### 发布与实测状态

- 91 发布 `weights/eic7700_platform/area-intrusion/area-intrusion_eic7700_1_5.pkg`，SHA-256 `dfbd301f3ea03382ec399e7447c25a71401e7fe941e606f1aeebeb7f86736784`；保留原 `1_4`。独立清单 `configs/eic7700_yolov5s_benchmark.json` 指定事件映射及 person/car 白名单；模型级包保留 80 类。
- 127 使用 `/home/ubuntu/workspace/test/yolov5s_int8_20260909/runtime_release` 独立运行库，media_agent 保持停止。8 轮压测全部通过：三轮 60 秒平均 NPU 路径 113.872 FPS、前后处理路径 73.695 FPS；各 600 秒持续测试分别 114.080 FPS/1.874734 TOPS 与 73.760 FPS/1.212132 TOPS。驱动完成数逐轮一致，NPU 均保持 1040 MHz；pipeline 最大单次延迟 206.990 ms，未据平均 FPS 承诺实时上限。新事件包 20 张图片验证通过，仅输出 person/car，已复制至板端 `models/eic7700_platform/area-intrusion/`。
- 正式文档：`docs/EIC7700边缘设备npu计算能力实测.md#algorithm-yolov5s`；完整证据：`outputs/diagnostics/yolov5s-int8-20260909/`，包含编译日志、量化表/编译图、精度指标、预测记录、包哈希与压力测试原始计数。

## 2026-09-03

### 1. SCJJ 安全帽与反光衣 EIC7700 平台事件包发布

#### 事件语义与模型映射

- SCJJ 五类模型已经直接输出 `no_hardhat` 和 `no_vest`，因此将 `hardhat-detection`、`vest-detection` 的模型来源统一由历史 `hardhat`/`ppe_safety` 切换为已完成 685 图精度验证的 `scjj` 量化模型；历史量化产物保留，但不再作为这两个事件的新包来源。
- `server/config/Event.yaml` 中 `hardhat-detection` 使用 `target_detection + target_labels: [no_hardhat]`；`vest-detection` 由 `nested_absence_detection` 改为 `target_detection + target_labels: [no_vest]`，删除 `primary_labels` 和 `secondary_labels`，不再通过 person 与 vest 二级框关系判断未穿反光衣。反光衣事件保留 `track_id_filter_enabled: true` 的去重行为。

#### 版本保护与打包实现

- `configs/eic7700_pipeline.json` 的包版本提升为 `1_5`，SCJJ 映射两个 PPE 事件，原 `hardhat`、`ppe_safety` 事件列表置空。
- 修正 `scripts/eic7700/package.py` 原有“事件目录非空即失败、`--force` 清空整个目录”的行为：事件目录现在允许多版本共存；默认只在同名版本文件存在时失败，`--force` 也只替换该同名文件，不删除其他历史版本。
- 使用 `--model-task scjj --event hardhat-detection --event vest-detection --package-version 1_5` 且不加 `--force` 完成发布，同时生成 `weights/eic7700/scjj/scjj_detect_eic7700_1_5.pkg`。

#### 发布产物与校验

- 新平台包为 `weights/eic7700_platform/hardhat-detection/hardhat-detection_eic7700_1_5.pkg` 和 `weights/eic7700_platform/vest-detection/vest-detection_eic7700_1_5.pkg`，SHA-256 均为 `7d09f95034d305dfceabc460e80d098f3cdf7fad260d5a44aca180d02a9c8b09`。二者共享同一个 SCJJ 模型和运行时配置，事件差异由目录名及 `Event.yaml` 的 `no_hardhat`/`no_vest` 过滤规则定义。
- 解包验证确认根配置为 `scjj_detect_eic7700.json`、RGB 输入、完整五类；包内 `.model`/量化表 SHA-256 为 `2dd90c9991a88b30ea93b4fcd57642c6c0ed5b4a49526e96fdd44664d398aecf`/`04f7fbc716db389f6ac321a33e0742b5fe90b91c8759fdbd552fcaa287341592`。
- 原 `hardhat-detection_eic7700_1_4.pkg` 和 `vest-detection_eic7700_1_4.pkg` 仍在原目录，发布前后 SHA-256 分别保持 `048d02bc0a7a198003827c7e8d0e2a0668b44aa7fdf1a45afd2ec307998a0858` 和 `8bba99c402e046b4ff41deb25964c166ecba99a85c9510875da7c9a8139223ca`。同版本二次发布按预期失败且新包哈希不变，验证没有覆盖历史或当前版本。
- 完整映射、命令、版本保护和校验方法已追加到 `vision-pipeline/docs/scjj_pt_to_eic7700_deployment.md`。本次完成量化服务器平台目录发布，未重启板端业务进程，也未执行真实平台任务或长稳验收。

### 2. media_agent 多模型同类目标重复告警修复

#### 根因与方案边界

- 确认同一通道多个模型均输出 `person` 时，旧推理兼容结果在扁平化后丢失模型来源，media_agent 会把全部模型的目标送入同一个 ByteTrack 和每一个事件检测器；跨模型框可能竞争轨迹并污染事件输入。
- `TrackPostStep` 原来在零目标帧提前返回，ByteTrack 的 `max_age` 没有推进；`target_detection`/`nested_absence_detection` 却会在 `track_exit_grace_ms` 后清除已报警状态。目标再次出现时 tracker 可能沿用原 ID，而事件已重新武装，形成同一 ID 重复告警。
- 仅修改 algorithm 无法在来源被上层丢弃后可靠恢复模型归属。最终采用 media_agent 最小来源透传、按模型跟踪和事件路由，algorithm 负责精确类别匹配、存活轨迹查询及事件锁存生命周期；未修改平台协议、告警 protobuf、事件阈值或模型输出。

#### 代码修改

- `algorithm/eic7700Infer` 与 `edgeDeploy` 的兼容 `object_result` 增加模型唯一键、来源路径和别名；RK3588 侧同步补齐 Pose 字段。`FmtInferPostStep` 将来源及原始结果索引保存在进程内元数据，不对外上报。
- `TrackPostStep` 改为每个模型独立维护 tracker，并在该模型零检测帧继续推进；ByteTrack 匹配从历史大类调整为模型内精确 `class_id`。新增 `tracker_list_live_track_ids()` 返回包含短暂漏检轨迹在内的未删除确认 ID。
- `EventPostStep` 依据任务 `model_config_name` 只向事件传入匹配模型的目标和存活 ID；Pose 关键点按原始结果索引精确回填。多事件场景不再把未知来源目标广播给所有事件，单事件旧后端保留兼容回退。
- `target_detection` 和 `nested_absence_detection` 在获得完整 tracker 生命周期时，短暂漏检不解除单次入区告警锁存；仅明确离开 ROI 或 tracker 确认删除 ID 时重新武装。未提供生命周期的新编译调用方仍回退 `track_exit_grace_ms`；日志新增 `explicit_outside`、`tracker_deleted`、`missing_timeout` 和 `event_reset` 原因。

#### 构建与验证

- algorithm 顶层 CMake 原来通过 `*/CMakeLists.txt` 自动扫描模块，服务器内 `eic7700Infer_save`、`eventEdge_save`、`tests_save` 会被误当成正式模块并重复声明 target。最终改为 `byteTrack`、`eic7700Infer`、`eventEdge`、`tests` 固定白名单；其他任意命名的备份、实验或临时目录均不会参与正式构建，也无需移动或删除这些目录。
- 在 92 服务器 SDK 202606 容器中以标准命令 `./scripts/build.sh --target riscv64 --jobs 4 --install` 完成重新配置、algorithm 相关静态/共享库、media_agent RISC-V 整包编译与安装。CMake 配置只加载四个白名单模块，重复 target 错误消失。
- 在 127 板端隔离目录 `/home/ubuntu/workspace/test/algorithm/tmp_event_fix_validation_20260903` 以临时库执行：`tracker_api_test`、`event_api_test` 均 PASS。覆盖不同类别不共用 ID、零目标帧推进、漏检超过 2 秒但 tracker 仍存活时不重复告警、tracker 删除后重新建立告警窗口。
- 完整根因、实现、兼容性和验证记录见 `docs/media_agent多模型同类目标重复告警修复与验证记录.md`。本次未替换板端生产二进制或共享库，未重启现有 media_agent；正式部署后的真实多模型任务验证仍待执行。

### 3. media_agent 新版本推理帧率回退定位、构建修复与部署

#### 分阶段定位及最终根因（2026-09-04 更正）

> 更正：本节早期把大幅回退主要归因于 media_agent 根工程把 Release 从 `-O3` 覆盖为
> `-O2`，这个构建问题真实存在，但“RGB 平面转换没有改变、与回退无关”的描述错误。
> 当时被比较的新旧库还同时包含不同的 RGB 处理实现，没有完成单变量 A/B。SCJJ 精度
> 初版修复在每帧对无缓存 DMA tensor 执行 `mmap + 409600 次 std::swap + munmap`，是另一个
> 独立热路径。最终结论必须同时包含构建优化等级和颜色转换实现，不能再表述为 O2 单一根因。

- 对 127 板端 `/home/ubuntu/workspace/test/media_agent` 与 `media_agent_old` 做受控切换验证。六路任务的新版本初始聚合推理约 `8～10 帧/秒`、进程 CPU 约 `165～179%`；旧版本同机同任务约 `30～31 帧/秒`、CPU 约 `115～124%`。CPU 百分比按单核 100% 口径，推理指标是六路合计，不是每路帧率。
- 首轮只指定 `--type Release` 重编译后仍为 `8.4～9.4 帧/秒`、CPU `171～179%`，证明仅切换构建类型名称不能解决问题。根工程 `CMakeLists.txt` 将 `CMAKE_CXX_FLAGS_RELEASE` 强制设为 `-O2 -DNDEBUG`，该普通变量也覆盖了 CMakeCache 中显示的默认 `-O3`；必须查看实际 `flags.make` 或编译命令，不能只凭缓存或二进制调试符号判断优化等级。
- 同时发现 `scripts/build.sh` 帮助文本称默认 Release，但实际初始化为 `RelWithDebInfo`，两者不一致。algorithm 独立构建的 Release 原本使用 `-O3`，随 media_agent 嵌入构建时却继承根工程 `-O2`，导致相同推理代码生成不同机器码。
- 逐模型 DEBUG 耗时曾定位到 DMA 图像预处理：回退版本约 `113～140 ms`，NPU 通常 `8～30 ms`、后处理约 `2～6 ms`；上层 track/event 通常 `0～1 ms`。改为真实 `-O3` 后，当时任务配置下聚合推理恢复约 `30～32 帧/秒`、CPU 约 `116～124%`，证明 `-O2` 是重要因素；但该对照没有隔离后来加入的 RGB CPU 平面交换，不能据此排除颜色修复的性能影响。
- EventPostStep 的逐目标路径规范化是额外开销，但不是此次大幅降速的主因：原 `readlinkat` 约 `928 次/秒`，缓存优化后短采样为 0，单独应用该优化时推理仍约 `9 帧/秒`。以上分阶段实测取代“指定 Release 即可恢复”或“事件路由是主要瓶颈”的初步判断。
- 对初版 RGB 精度修复单独检查确认：每帧 640×640×3 tensor 在 VPS 完成后仍由 CPU 映射无缓存 DMA，逐字节交换 B/R 两个 409600 字节平面。该实现恢复了精度，却把颜色适配放进每模型、每帧的 CPU 热路径；用户回退该提交后帧率恢复，原因不是 RGB 契约本身，而是实现方式。

#### 最终代码修改

- `media-agent/scripts/build.sh` 实际默认值统一为 Release；根 `CMakeLists.txt` 的 Release C++ 优化改为 `-O3 -DNDEBUG`。以后仍可使用原命令 `./scripts/build.sh --target riscv64 --jobs 16 --install`，不要求额外添加 `--type Release`；显式指定仅用于记录构建意图。
- `media-agent/src/detector/AlgoWorkflow.h`、`EventPostStep.cpp`：对模型路径规范化增加实例级缓存，优先原始路径/别名字符串匹配；每帧按模型组织目标和存活轨迹，将同模型的多个事件合并调用，重复事件名在初始化时去重。保留跨模型隔离、单事件旧接口兼容和原始 Pose 索引映射。
- 无目标、无 tracker 状态的事件仍接收空帧，保证离岗等时间型判定持续推进；多事件场景未知来源目标不会借此广播到其他模型。显式为空的存活 ID 集合仍与“未提供生命周期”区分。
- `algorithm/eic7700Infer/src/edgeInfer.cpp` 新增 DEBUG 级逐模型预处理、推理、后处理及预处理复用耗时，便于后续定位；部署完成已恢复两份运行日志配置为 `info`。未调整平台任务阈值、ROI、模型包、追踪参数或告警去重策略，未修改 91 主机系统环境。
- `algorithm/eic7700Infer` 保留 `input_format` 契约：顶层字段优先于 `preprocess.input_format`，检测模型未声明时默认 BGR，分类模型未声明时默认 RGB，颜色顺序进入预处理复用签名。最终实现继续使用 SDK 202606 支持的 `B8G8R8I_PLANAR`，RGB 模型仅把 VPS 目标 B/R plane offset 反向绑定，使硬件直接写出连续 RGB tensor；已删除帧级 CPU `mmap/std::swap/munmap`。
- `eic7700_stream_infer_test` 新增 `--preprocess-only`、`--dump-preprocessed` 和 `--dsp-id`，可在不运行 NPU/DSP 后处理时定量回归预处理耗时和输入字节，也可在板端 DSP 设备状态允许时选择独立 DSP 做完整评估。

#### 构建与定向回归

- 92 服务器 SDK 202606 容器 `cross-proj-202606-cdky` 中，不传 `--type`，使用 `./scripts/build.sh --target riscv64 --jobs 4 --install` 完成默认 Release 编译和安装；降低并发仅用于本次验证。实际 media_agent 和 `cdky_eic7700_infer` 的 `flags.make` 均确认包含 `-O3 -DNDEBUG`。
- 2026-09-04 在同一 SDK 202606 容器对最终 algorithm 执行 `./scripts/build.sh --target riscv64 --type Release --jobs 16 --clean --build-tests --install`，干净构建及安装成功。最终库 SHA-256 为 `c4c67c676c4078d0e0b3f8bcec420fcc3d22e01d606c91145ee289fe7ebcedc2`，测试程序为 `93a585f65f8e2188683ddb40d88214523adf3d00af7f86d88358ba240ae24a27`。
- 同一首帧分别使用旧 CPU-swap 库和最终 VPS-offset 库导出 NPU 输入，两份 1,228,800 字节文件 SHA-256 均为 `0162063e93ed1133d84724c99a29093b5d97a044957052ef4ac2f56efadfb03d`，`cmp` 无差异。最终 tensor 与 RGB letterbox 的相关系数为 `0.999321`，与 BGR 为 `0.903378`，证明颜色正确且与已完成 685 图精度复评的旧修复数值等价。
- 30 图 ABBA 预处理专测：旧 CPU-swap 两轮平均 `16.033/15.879 ms`（`62.37/62.98 FPS`），最终 VPS-offset 两轮为 `2.442/2.449 ms`（`409.45/408.40 FPS`）；最终实现预处理约快 `6.51×`，两轮仅差 `0.006 ms`，不再存在 CPU 通道交换造成的帧率回退。
- 新增 `media-agent/tests/detector/event_route_test.cpp` 与可选构建目标，默认关闭，不安装进业务包。启用方式：`./scripts/build.sh --target riscv64 --jobs 4 --cmake-arg -DMEDIA_AGENT_BUILD_EVENT_ROUTE_TEST=ON`，产物为 `build-riscv64/bin/event_route_test`。
- 在 127 隔离目录 `/home/ubuntu/workspace/test/algorithm/tmp_event_fix_validation_20260903` 执行 `LD_LIBRARY_PATH=/home/ubuntu/workspace/test/media_agent/lib ./event_route_test`，全部 PASS：同类目标按模型隔离、同模型多事件合并、重复事件去重、短暂漏检与已删除轨迹、启动零目标空帧、多事件未知来源隔离、Pose 原始索引、告警拷贝、单事件兼容、共享模型别名及路径缓存。该测试使用事件 C API 桩，不启动 NPU/VDEC 或业务事件实例。
- 同目录 `tracker_api_test`、`event_api_test` 使用当前正式目录的共享库再次全部 PASS。以上为定向逻辑及性能回归，不等同于重新完成四类模型全量精度评估或长期真实事件验收。

#### 部署、备份与运行状态

- 2026-09-03 的 media_agent O3 主程序、事件路由和空帧补丁部署记录仍作为历史阶段保留，其不同任务配置下的 `30～32` 与 `23～25 帧/秒` 只说明 O2 构建问题已修复，不能再用于证明初版 RGB CPU-swap 没有开销。
- 2026-09-04 在确认 `media_agent` 完全停止后，将最终 `libcdky_eic7700_infer.so` 原子部署到 `/home/ubuntu/workspace/test/algorithm/lib` 和 `/home/ubuntu/workspace/test/media_agent/lib`，两处 SHA-256 均为 `c4c67c676c4078d0e0b3f8bcec420fcc3d22e01d606c91145ee289fe7ebcedc2`；测试程序部署到 `algorithm/bin`，SHA-256 为 `93a585f65f8e2188683ddb40d88214523adf3d00af7f86d88358ba240ae24a27`。
- 替换前 algorithm 库 `8971b876...`、media_agent 库 `f4b8886b...` 和测试程序 `351152a2...` 已备份到 `/home/ubuntu/workspace/test/backups/algorithm-rgb-vps-offset-20260904_1518`。部署后 `--preprocess-only` 3 图冒烟为 `2.542 ms/张`；按用户要求未启动 media_agent，最终检查无 `media_agent` 进程。
- 板端当前 DSP driver 对 DSP 0～3 的独立 `DetectionOut` 均返回 `0xa014602c`，内核记录 `process_id=0 shouldn't load_op`；该错误在旧 CPU-swap 和最终 VPS-offset 两版相同，且出现在颜色预处理与 NPU 已完成之后，不是本次代码引入。为避免擅自重启共享开发板，本次使用“输入 tensor 字节完全一致 + 既有 685 图预测重新评估”的等价性闭环；恢复 DSP 全局状态后仍应补跑一次最终库的 685 图完整推理，但不影响当前颜色与性能根因结论。

### 4. vision-pipeline EIC7700 打包支持稀疏输出标签白名单

#### 根因与语义

- 91 服务器 SCJJ 运行时 JSON 删除 `class_names["0"] = "person"` 后，`package.py` 将输出白名单误当作完整训练类别表，因缺少 ID 0 报 `source class_names is sparse`。algorithm 已支持稀疏映射，缺失 ID 在 CPU/DSP 后处理中被过滤；模型真实维度仍由 `num_class=5` 定义，不能修改为 4 或重编号其他类别。

#### 修改与兼容

- 修改本地及 91 的 `vision-pipeline/scripts/eic7700/package.py`：允许非连续及空输出白名单，保留原 ID，不按剩余数量重排或补回标签；旧数组允许空/null 槽位。保留字段类型、非负/越界 ID、模型文件、量化表及版本保护校验。
- 模型级包与事件包都遵从源配置白名单；COCO 平台公开类别清单取交集，不恢复源配置禁用的类别。只检查本次选择事件的标签依赖，缺失依赖由硬错误改为 WARNING，不将运行时输出策略作为拒绝打包的依据。
- 保留 91 已有“多版本共存、--force 仅替换同名文件”及“只生成模型包时不需要 Event.yaml”的行为。本地脚本同步这些既有能力，没有用本地旧版本覆盖服务器逻辑。
- 在 91 当前已更名的 `docs/pt_to_eic7700_deployment.md` 第 13 节补充字段语义、原 `1_6` 发布命令、精度评估边界和测试方法，并同步本地同名文档。全五类评估必须使用完整白名单，否则 person 被过滤导致的指标下降不是量化误差。

#### 验证与部署边界

- 新增 `scripts/eic7700/tests/test_package.py`。在 91 用 `EIC7700_SMOKE_SOURCE=weights/eic7700/scjj python3 -B -m unittest discover -s scripts/eic7700/tests -v` 执行 9 项测试全部通过，耗时约 5.4 秒；不使用 GPU/NPU，不安装依赖。
- 真实 SCJJ 在临时目录完成模型包及两个事件包加密打包、解包验证：类别为原始 ID 1/2/3/4，`num_class=5`，`.model`/量化表哈希不变，两个事件包字节一致。另验证缺失事件标签仅警告、未选模型无配置也不阻塞、同版本拒绝覆盖及历史版本保留。
- 用户源 JSON SHA-256 保持 `6b40eabad99f50f8db0e23afddf58ec290fae6b2b9cc79a9f03ecd34ab7737cf`，正式目录既有模型包与事件包均未改变。本次只修复脚本并做临时冒烟，未正式发布 `1_6`，未修改 algorithm/media_agent 或板端进程，也未修改 91 主机系统环境。原脚本与测试日志保存在 91 项目 `.codex-tmp/package-sparse-20260903`。

## 2026-09-02

### 1. SCJJ 五类模型 EIC7700 量化、模型级打包与精度评估

#### 范围与模型边界

- 量化输入固定为 91 服务器 `vision-pipeline/weights/train/scjj/scjj_detect_cls5_yolov8s_ep86_p09559_map5009675_map509507598_20260902_190437_640.pt`，输入 640×640、类别顺序为 `person/hardhat/vest/no_hardhat/no_vest`，PT SHA-256 为 `e1a5e7bc7fbe30e9d3c81f6f23c8d4a66eb21598de6d663bee1f74de0d9fadb9`。
- `weights/train/scjj_v3` 是输入 960×960 的预标注模型，与本次量化无关。按要求等待其 100 轮训练进程自然退出并释放显存后才启动量化，没有终止训练，也没有把 v3 权重用于导出、校准、打包或评估。
- 数据集固定为 `data/dataset/scjj`，训练/验证图片分别为 2588/685 张；量化校准和分析只从训练集确定性选取 200/20 张，验证集只用于独立精度评估。

#### 流程支持与量化产物

- `vision-pipeline/configs/eic7700_pipeline.json` 新增 `scjj` 任务；`scripts/eic7700/quantize.sh` 支持从任务清单向通用量化流程传递 `quantized_dtype` 和 `nodes_i8`。SCJJ 采用全局 INT16、首层 `/model.0/conv/Conv` INT8、三个检测输出 INT16 的精度优先配置。
- `scripts/eic7700/package.py` 将 `--event-config` 调整为仅在生成事件包时必需，允许 `--model-task scjj --skip-event-packages` 只生成独立推理/精度评估所需的模型级包；同时禁止模型包和事件包同时跳过。
- ONNX 导出、EsQuant 155 算子量化与 20 图逐层分析、EsAAC 编译均成功。EIC7700 ONNX、`.model`、量化表和运行时配置均位于 `weights/eic7700/scjj`；`.model` SHA-256 为 `2dd90c9991a88b30ea93b4fcd57642c6c0ed5b4a49526e96fdd44664d398aecf`，`esquant/table.json` 为 `04f7fbc716db389f6ac321a33e0742b5fe90b91c8759fdbd552fcaa287341592`。
- 模型级包 `weights/eic7700/scjj/scjj_detect_eic7700_1_4.pkg` 通过内容和根配置自检，保留全部五类，SHA-256 为 `b1cc7204158581ade434977a45e7063e397b1851fce7b1312f9888ab7a0b1146`。本次 `weights/eic7700_platform` 新增文件数为 0，未生成或发布事件/平台包。

#### 板端推理与量化前后评估

- 因验证图片位于点位子目录，按 `val.txt` 建立带点位前缀的 685 图/685 标签相对软链接视图，避免 Ultralytics 目录输入不递归造成漏图，并保证 PT、板端与真值按同名文件严格对齐。PT 和板端均使用 `confidence=0.001`、`IoU=0.7`、`max-det/topk=300` 导出候选，不保存全量可视化图片。
- pkg 和验证视图部署到 127 板端 `/home/ubuntu/workspace/test/algorithm/evaluation/scjj_20260902_201500`。在不停止现有 `media_agent` PID 19595 的条件下，`eic7700_stream_infer_test` 完成 685/685 张图片且退出码为 0；耗时 180.633 秒、约 3.792 图/秒、峰值 RSS 139,648 KiB。该吞吐包含现有业务并发，不作为空闲板性能基线。
- `vision-pipeline/evaluation/yolov8_det_quantization` 同集评估结果：PT/EIC7700 的 mAP50-95 为 `0.7291/0.4655`，mAP50 为 `0.9510/0.7357`，mAP75 为 `0.8420/0.5049`；small/medium/large AP 分别由 `0.4317/0.6857/0.8256` 降至 `0.1224/0.3919/0.5587`。
- person、hardhat、vest、no_hardhat、no_vest 的 mAP50-95 分别由 `0.7781/0.7327/0.8157/0.6410/0.6778` 降至 `0.6627/0.3753/0.4806/0.3634/0.4454`。0.55 阈值下 PT/EIC7700 F1 为 `0.9417/0.7530`；阈值扫描的最佳总体 F1 分别为 PT 0.9470（0.30）和板端 0.7903（0.20）。降低板端阈值不能弥补 AP、定位质量和小/中目标召回损失。
- 结论为量化、编译、打包和板端功能验证成功，但当前模型存在明显量化精度损失，尤其是 hardhat、vest 和小目标，不满足直接发布条件。完整命令、路径、哈希、指标、阈值扫描及后续优化约束记录于 `vision-pipeline/docs/scjj_pt_to_eic7700_deployment.md`；评估原始结果保存在 `evaluation/yolov8_det_quantization/work/scjj_20260902_201500`。

#### 阶段补充：量化精度损失根因定位与 RGB 输入修复

- 进一步检查 EsQuant 图级输出相似度、EsAAC 编译输出和板端原始 NPU 输出后，确认量化模型三个检测头仍为 INT16，图级余弦相似度为 `99.993%`～`99.999%`。对同一图片独立解码 NPU 原始输出，所得框、类别和置信度与现有 DSP 后处理逐项一致，排除了量化表、编译、pkg 打包和 DSP 后处理作为本次大幅损失的主因。
- 对板端实际送入 NPU 的 640×640 平面张量进行导出比对：修复前其与 BGR letterbox 的相关系数为 `0.999348`、MAE 为 `0.392696`，与 RGB 的相关系数仅为 `0.903411`、MAE 为 `4.346139`。SCJJ 训练、ONNX 导出和量化配置约定输入为 RGB，但 EIC7700 VPS 只能输出 `B8G8R8I_PLANAR`；原硬件预处理仅为分类模型交换 B/R 平面，检测模型因而实际接收 BGR。安全帽、反光衣等小目标对颜色更敏感，表现为置信度和召回大幅下降。
- `algorithm/eic7700Infer` 新增显式 `preprocess.input_format` 输入颜色契约：兼容旧检测包的默认值仍为 BGR，分类模型保留原 RGB 默认；SCJJ 等显式声明 RGB 的模型在 JPEG 和 DMA 两条硬件预处理路径中统一交换 B/R 平面。预处理复用签名同时加入颜色顺序，避免 RGB/BGR 模型错误复用输入张量。
- 92 服务器使用 SDK 202606、`--target riscv64 --jobs 8 --build-tests` 完成交叉编译。由于仓库中已有用户备份目录会造成重复 CMake target，构建期间只将其临时移到工程外并通过 trap 恢复，没有删除或修改这些目录；安装阶段仍受已有 include 文件权限限制，但目标库和带安装 RPATH 的测试程序均已成功生成。
- 修复后对相同 685 张验证集重新执行板端推理，处理 685/685、退出码 0，耗时 205.417 秒、约 3.335 图/秒；该数据仍包含运行中的 `media_agent` 并发负载。结果保存在板端 `evaluation/scjj_20260902_201500/results_rgb_fix/image_results.jsonl`，并回传到量化服务器同名评估目录的 `board_rgb_fix`，分析产物位于 `analysis_rgb_fix` 和 `thresholds_board_rgb_fix`。
- 修复后 PT/EIC7700 的 mAP50-95 为 `0.729074/0.726127`，差值从 `-0.263606` 收敛到 `-0.002947`；mAP50 为 `0.950991/0.948889`，mAP75 为 `0.841972/0.839006`。0.55 阈值下 PT/板端 precision、recall、F1 分别为 `0.975874/0.909800/0.941680` 和 `0.974441/0.910030/0.941135`。匹配框保留率为 `0.994830`、平均 IoU 为 `0.977593`、置信度 MAE 为 `0.004530`，证明剩余差异属于可接受的量化/数值误差。
- person、hardhat、vest、no_hardhat、no_vest 的修复后板端 mAP50-95 分别为 `0.776381/0.731483/0.812443/0.636187/0.674141`，对应 PT 为 `0.778084/0.732711/0.815706/0.641036/0.677834`。此前“pkg 存在明显量化精度损失、不满足发布条件”的结论由本阶段结果取代；pkg 本身无需重新量化或重新打包，本次仍未发布到 `weights/eic7700_platform`。
- 修复版库已原子部署到 `/home/ubuntu/workspace/test/algorithm/lib/libcdky_eic7700_infer.so` 和实际生产加载路径 `/home/ubuntu/workspace/test/media_agent/lib/libcdky_eic7700_infer.so`，二者 SHA-256 均为 `8971b87600dab7470cb30b922f384370909b33566126ec54b320e28505b3f866`；测试程序 `algorithm/bin/eic7700_stream_infer_test` 的 SHA-256 为 `351152a25247ef33f8e8dc5111d3ac1b949a38ef6cc67457ef033a6b1430a394`。替换前文件保存在同目录的 `.before-rgb-input-fix` 备份，其中生产旧库 SHA-256 为 `a5bf99ec044fba5474bded2df5322e510fd7f0b96830a3588bf4c930b97b1103`。部署使用临时文件加原子重命名，没有停止 PID 19595；`/proc/19595/maps` 确认该进程继续映射已删除的旧 inode，业务不中断。需在业务允许时受控重启后才会加载生产路径上的修复库，本次未擅自重启。

## 2026-09-01

### 1. EIC7700 五路事件端到端 CPU 与长时内存深度分析

#### 调用链与模型共享

- 梳理 `RTSPPuller -> AsyncVideoDecoder/VDEC -> RoundRobinInferScheduler -> AlgoDetector -> EdgeInfer -> Fmt/Track/Event -> Recorder/Snapshotter -> IpcClient` 的完整生产路径，确认 algorithm 测试程序的 FFmpeg/OpenCV/可视化代码不进入上层调用。
- 当前 5 路任务共 17 个事件绑定，但按解密模型内容键去重后只有 6 个模型执行器。area/crowd 共享区域模型，fall/prolonged-lying/help/posture/dangerous-zone 共享人员 Pose；fight 使用独立 Pose 模型。
- 上层帧级推理约 42.4/s；按当前流-模型共享关系估算底层模型运行约 93.2/s。报告已明确该值是推算而非分模型生产计数，避免将 17 个事件的模型 CPU 重复相加。

#### CPU 实测与热点

- 60 秒 `pidstat` 中进程平均 88.77% 单核（user/system=44.29/44.48%）。六个模型执行器合计 37.71%，三个上层 infer/track/event 线程合计 5.32%，VPS/NPU 辅助线程约 6.70%，拉流、解码、发布和活跃截图线程分别约 7.25%、21.88%、5.00% 和 1.72%。
- 独立 60 秒 `perf stat` 记录约 9,885 context-switch/s、748.5 migration/s、5,851.9 page-fault/s，IPC 为 0.556。120 秒 cpu-clock 采样无丢失，证明当前同时存在重后处理标量读取和 DMA/IOMMU/cache 同步开销。
- 共享人员 Pose、区域 detect、打架 Pose、火焰 detect、反光衣 YOLO26 和手机 detect 执行器分别约占 9.95%、7.97%、6.87%、6.03%、4.73% 和 2.17% 单核。区域执行器以 system CPU 为主，热栈为跨核 cache flush、SMMU 和 DMA-BUF map/unmap。
- Pose `tensor_decode::read_value` 占全进程 perf 样本 8.70%；YOLO26 的 `expf`/私有 `read_value` 分别占 1.58%/1.53%。YOLOv8 CPU 后处理还在逐帧用 `std::regex` 解析不变输出名，perf 可见 locale/regex/string 和分配开销。
- `libcdky_event`/`libcdky_track` 直接叶子样本换算只约 0.90%/0.39% 单核。已找到 Pose IoU 回配、告警框二次匹配、tracker/event 逐帧 vector/set 分配等无效开销，但其优化优先级低于模型后处理。

#### 长时内存新结论

- 不中断当前进程，对已连续运行约 14 小时的 PID 157023 做只读检查；任务仍为 `5/5/0/0`，最终 algorithm 库 SHA-256 仍为 `a5bf99ec044fba5474bded2df5322e510fd7f0b96830a3588bf4c930b97b1103`。
- RSS/PSS 首个长时样本为 799,656/796,323 KiB，其中 18 个大型匿名映射合计 RSS/dirty 758,208 KiB，大小和 64 MiB 地址对齐特征符合 glibc 多线程 malloc arena。30 秒四个样本 RSS 不变、FD 在 288～292 间波动，但约 16 分钟后 RSS/PSS 又增至 807,812/804,476 KiB，大型 arena RSS 增至 765,816 KiB。因此无秒级快速线性泄漏证据，但高水位仍在分钟级阶跃上移，不能宣布长时内存已稳定。
- 当前进程期间累计约 7,621 次截图、3,428 次录像启动，且有 3 路任务拆除/重建。`ModelService` 只保存 `weak_ptr` 并会清理过期执行器，暂未找到 algorithm 永久强引用泄漏；当前证据更支持大块截图/FFmpeg/模型重建分配使 arena 保留高水位，但必须用 24 小时受控 A/B 分离归因。

#### 优化方案与变更边界

- P0 先实施 Pose typed reader/索引预计算、YOLO26 先求最大 logit 后单次 sigmoid、模型 open 时固化输出 descriptor 和有上限 scratch buffer。这些优化不改变 SDK 资源生命周期，但必须逐框/逐关键点验证。
- P1 再用单模型小步 A/B 验证输出持久 mmap；NPU prepare/unprepare 持久化风险更高，必须通过模型重载、强制 kill 和断电冷启动后才可采用。不恢复优化 VPS 全局锁，其实测等待仅约 0.01% 单核。
- 内存先做默认 allocator 与 `MALLOC_ARENA_MAX=4` 的 24 小时对照，并分离“保留推理、关闭截图/录像”负载。Snapshotter 固定 job buffer 池属于 media_agent 后续优化，不与 algorithm 首批 CPU 补丁混合。
- P2 再传递 raw index/关键点以消除 EventPostStep O(n²) 匹配，并复用 track/event scratch buffer。完整调用链、实测表、内存边界、分批方案和验收门槛已记录到 `docs/EIC7700边缘设备npu计算能力实测.md#production-resources`。本阶段仅做只读生产剖析和文档固化，未重启进程、未更换板端库、未修改生产代码。

### 2. algorithm 历史轨迹状态内存泄漏最小修复

#### 根因与修改

- 实测确认 `TargetDetectionEventDetector::m_trackStates` 与 `NestedAbsenceDetectionEventDetector::m_trackStates` 会为递增的 `tracker_id` 插入节点，但离区或超过 `track_exit_grace_ms` 后只重置 value、不删除 map key。历史 ID 因此在 event handle 生命周期内永久保留，并使每帧清理遍历成本持续增长。
- 最终只修改两个事件 `.cpp`：把清理循环改为 iterator 遍历，在非入区旧状态、明确位于 ROI 外、超过离区容忍时间三种情况下执行 `erase(it)`。保留区内刷新和漏检容忍逻辑；同 ID 重入仍由原路径创建新状态，没有修改事件阈值、ABI、跟踪、推理、NPU/VPS、media_agent 或系统恢复代码。
- 复核 algorithm 推理、前后处理、ModelService、ModelExecutor 队列、DMA 输出池、ByteTrack、running/reverse/Pose 状态和 event handle 生命周期，没有发现第二处随帧或历史 ID 无界持有的生产对象。供应商 NPU/VPS/VDEC 内部不在 x86 sanitizer 覆盖范围，结论边界已明确记录。

#### 定向 A/B 与测试

- 原生同一 event handle 连续生成并淘汰 10,000 个唯一 ID：target/nested 修复前仍持有 7,840,704/7,996,000 B，修复后仅增加 2,320/608 B；耗时由 274/266 ms 降为 5/6 ms。
- 原生 Release 的 `common_config_test`、`event_api_test`、`tracker_api_test` 全部通过；ASan/LSan/UBSan Debug 构建运行相同 3 项也全部通过且无报告。SDK 202606 RISC-V 显式测试构建和安装通过。
- 板端临时库执行 `event_api_test` PASS；相同 10,000 ID 压测残留为 2,320/656 B。最终 `libcdky_event.so` SHA-256 为 `2aa6a28d0b8e66c8f8b3da6f3650d0b9c4cf9c5c98cbece8f6bef0bbe20c41f5`。

#### 部署与短稳

- 修复库已部署到 127 的 algorithm 与 media_agent `lib` 目录；旧库备份位于 `/home/ubuntu/workspace/test/backups/algorithm-track-state-leak-20260901_171539`。
- 板端 `media_agent` PID 19595 五路任务达到 `5/5/0/0`，稳定窗口 `recv/dec/pub≈129/s`、`infer≈32/s`，事件告警、截图和录像正常；约 7 分钟时线程 71、FD 291、DMA-BUF 约 240，NPU/VPS/inference failed、`bad_alloc` 和段错误均为 0。
- 中途 `sp00ct3`、`sp4sa1b`、`sppnecd` 三路上游 RTSP 源曾持续返回 `Invalid data found when processing input`，任务短暂变为 `5/2/3/0`，剩余两路仍持续推理；随后现有自动重连恢复，最终重新稳定为 `5/5/0/0`、`infer≈32/s`。全过程未见 algorithm/NPU/VPS 错误；本轮仅记录，未扩大范围修改拉流逻辑。
- 总进程 RSS 在启动和密集告警期间仍呈 malloc arena/媒体大块缓冲高水位阶跃，不能与已定向证明消除的 map 持有泄漏混为一谈。完整代码审计、A/B 数据、构建部署和结论边界见 `docs/algorithm_trackStates内存泄漏修复与验证记录.md`。

## 2026-08-31

### 1. EIC7700 异常重启后 media_agent 冷启动与系统时间修复

#### 问题与根因

- 127 板卡异常重启后 `es_media_srv` 不存在，直接执行 `sudo ./media_agent` 时出现 `Bad file descriptor`、`BMS_Bootup`、`IPCSK_InitClient`、`SYSEVENT`、`vpsBind fd=-1` 和 `ES_VPS_Init failed`。先运行非 sudo 的 algorithm 测试程序可恢复，是因为该程序以 `ubuntu` 用户启动了 `es_media_srv`，并非模型推理本身修复了 VPS。
- `strace` 确认 root 路径失败于 `openat("/var/tmp/bms.lock", O_RDONLY|O_CREAT, 0444) = -1 EACCES`。`/var/tmp` 是 sticky 目录、锁文件属于 `ubuntu`，板卡 `fs.protected_regular=2`；SDK 守护进程由 sudo/root 启动时，带 `O_CREAT` 打开其他用户的文件会被内核拒绝。重复 `ES_SYS_Init`/`ES_SYS_Exit` 不能解决该 UID 所有权约束。
- 未采用删除 `/var/tmp/bms.lock`、更改其属主或关闭 `fs.protected_regular` 的方案，避免破坏健康 BMS 实例的锁所有权或降低系统级安全保护。
- 重启后时间回退的原因不是 `systemd-timesyncd` 未重复安装，而是平台软件包固定安装并启用了 `chrony`、同时移除了 `systemd-timesyncd`；现有 chrony 配置只 include 一个空的 `/etc/chrony/conf.d/99-smart-guard-managed.conf`，导致服务 active 但没有任何 NTP source。

#### algorithm

- 新增公共接口 `infer/eic7700_runtime.h`：`ensure_system_runtime_ready()` 和 `release_system_runtime()`，在进入并发 VDEC/VPS 初始化前统一管理 EIC7700 SYS/BMS 生命周期。
- `ensure_system_runtime_ready()` 以 `/proc/net/unix` 中处于监听状态的 `@es_bms_abstract` 作为真实就绪条件，不再把 `ES_SYS_Init == ES_SUCCESS` 误判为后台服务已可用；超时会配对清理并让调用方快速失败。
- 当进程由 sudo/root 启动、BMS 尚未就绪且存在合法 `SUDO_USER` 时，仅通过 `/usr/sbin/runuser` 将 `/usr/bin/es_media_srv` 以 sudo 原始用户启动；标准输入输出重定向到 `/dev/null` 并与主进程解耦。随后 root 主进程再执行自己的 `ES_SYS_Init`。这样同时满足 `bms.lock` 的 UID 约束和平台 root socket 的访问要求。
- 保留非 sudo algorithm 程序的原有路径：普通用户直接调用该接口时不会进行身份切换。

#### media-agent

- EIC7700 构建在 `Pipeline` 创建工作线程之前调用 algorithm 的运行时就绪接口；失败时记录 critical 日志并退出，避免在无效 IPC fd 上继续并发初始化 VDEC/VPS。
- Pipeline 启动失败或正常停止后配对调用运行时释放接口。除这一启动/退出挂钩外，未修改 media_agent 业务、解码、推理或事件代码。

#### 板端时间同步

- 保留平台选定的 chrony，未继续安装与其冲突的 `systemd-timesyncd`。
- 在 `/etc/chrony/conf.d/98-cross-proj-default.conf` 增加 `pool ntp.ubuntu.com iburst maxsources 4`，并在 `/etc/chrony/chrony.conf` 显式 include；原配置备份为 `/etc/chrony/chrony.conf.pre_cross_proj_20260831`。
- 冷重启实测：板载 RTC 初始仍为错误时间，网络就绪后 chrony 自动切换为 Stratum 3、`Leap status: Normal`，系统时间校正到 `2026-08-31`，无需再次执行 apt 安装。

#### 构建、部署与验证

- 92 服务器 SDK `202606` 容器中，algorithm 与 media-agent 均执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 并通过。92 的 protocol 子模块版本落后于 media-agent 源码，首次干净生成 protobuf 会缺少 `active_periods`；构建时临时使用本地匹配的 `media-agent.proto`，结束后恢复服务器原文件，未把该无关差异混入修复。
- 最终产物 SHA-256：`media_agent` 为 `809653e6685795c47d30196abe1bad84405b4ba68840cb2f6879e325d9c06e75`，`libcdky_eic7700_infer.so` 为 `43257cdf99075d4fb807f0771b2c405a66a832e2d01352e3f72874c735243327`；共享库已同时部署到板端 media_agent 和 algorithm 安装目录。
- 最终冷启动前确认新 `boot_id=e2a1f76f-6add-4c81-bb02-201cacab5446`、`es_media_srv` 不存在、BMS socket 数量为 0，且未运行 algorithm 测试程序。直接执行 `sudo ./media_agent` 后日志显示 `EIC7700 BMS service started as sudo user=ubuntu`、`ES_SYS_Init bootstrap ret=0x0 errno=0` 和 `EIC7700 SYS/BMS runtime ready`。
- 35 秒真实平台任务验证达到 `total/suc/fail/disable=5/5/0/0`，完成 RTSP 解码、推理、告警、截图和录像；TERM 后 5 个 VDEC group、全局 VDEC、Pipeline 均正常释放并 clean exit。应用日志及本次 journal 中目标错误关键字均为 0。
- 同一 boot 内再次执行 20 秒启动验证同样达到 `5/5/0/0`，只保留 1 个 `ubuntu` 所属的 `es_media_srv`，没有重复拉起或资源泄漏。
- 使用最终 algorithm 共享库执行安全帽模型图片推理，20/20 张处理完成、退出码为 0；`ldd` 无缺失依赖，运行时接口导出符号检查通过。

### 2. algorithm EIC7700 生产依赖收敛、资源分析与前处理优化

#### 问题与结论

- `media_agent` 的生产链路向 algorithm 传入 VDEC 解码后的 NV12 DMA fd，只调用 `EdgeInfer -> ModelExecutor -> Eic7700ModelPluginBase`，不使用 OpenCV 图片加载、CPU resize、画框或保存图片；这些功能只属于 `eic7700_stream_infer_test` 的图片输入与可视化路径。
- 五路真实任务的函数级 CPU time 表明：模型后处理约占 23.5% 单核，tensor padding 约 6.4%，NPU 调用的 CPU 部分约 4.2%，VPS 约 2.5%；NPU run 的约 24.7 ms wall time 主要是硬件等待，不是 CPU 计算。输入 DMA 每帧分配+释放只约 0.85% 单核，不足以支持引入会增加常驻内存和异常清理复杂度的 DMA 池。
- 同一帧多模型已有完整签名缓存，输入 tensor 和预处理契约完全一致时只执行一次前处理；测试专属输入准备和可视化不会进入上层调用。

#### 最小修改

- algorithm `scripts/build.sh` 默认由构建测试改为 `BUILD_TEST=OFF`；需要 API、单元和完整流测试时显式使用 `--build-tests`。algorithm 顶层 CMake 也声明默认关闭的 `BUILD_TEST`，确保作为 media-agent 子目录编译时行为一致。
- OpenCV 改为仅在显式测试或显式开启 `EIC7700_BUILD_LEGACY_IMAGE_API` 时查找；生产共享库移除 OpenCV include/link 和未使用的 `detection_utils.cpp`，遗留 `YoloV8Det` 图片/文件接口默认不编译但保留可选兼容开关。
- `eic7700_stream_infer_test` 的 JSONL 每个模型结果新增 `preprocess/inference/postprocess` 毫秒值及 `shared_preprocess`，用于不启用可视化的阶段统计；未修改 media_agent 业务代码、VPS/NPU 参数、模型处理算法和结果结构。
- 增加默认关闭的 algorithm 函数剖析器：设置 `EIC7700_PROFILE=1` 或 `profile=true` 时，使用线程 CPU 时钟和 wall 时钟每 30 秒汇总 DMA 分配/释放、padding、VPS 锁等待、VPS normalization、RGB plane 交换、整段前处理、NPU run 和模型后处理。固定原子计数器无逐帧分配，生产默认配置不输出剖析日志。
- 根据热点数据只修改 `fill_tensor_padding`：先计算 VPS 最终有效 `dst_rect`，只写 VPS 不会覆盖的 letterbox 边框和对齐尾部；VPS 覆盖整个对齐 tensor 时跳过 mmap/padding。保留各 plane 原量化 padding 值，不改变 tensor ABI、VPS 参数、模型、后处理、NPU 并发和 media_agent 业务。
- 完整分析、测试方法和后续优化约束记录在 `docs/EIC7700边缘设备npu计算能力实测.md#algorithm-yolov8`。

#### 构建与板端验证

- 92 的 SDK 202606 容器中，algorithm 默认干净构建显示 `BUILD_TESTS=0`，安装目录没有测试程序，生产 `libcdky_eic7700_infer.so` 无 OpenCV `DT_NEEDED` 和遗留 `YoloV8Det` 动态符号；显式 `--build-tests` 后两个 EIC7700 测试程序均构建成功，只有测试二进制依赖 OpenCV，共享库仍不依赖。
- media-agent 默认干净配置为 `BUILD_TEST:BOOL=OFF`，目标列表没有 algorithm API/完整流测试；完整 riscv64 构建安装通过，`media_agent` 及其 algorithm 推理库均无 OpenCV ELF 依赖。
- 127 使用 hardhat 1.4 模型、`hat2.mp4`、关闭可视化测试 300 帧：稳定段 280 帧的前处理/推理/后处理平均耗时分别为 1.531/7.427/1.792 ms，P95 为 1.560/7.445/1.846 ms，三阶段平均合计 10.750 ms；处理吞吐 51.52 FPS，进程平均 CPU 37%，峰值 RSS 88,704 KiB。
- 单路新旧库严格 A/B 中，稳定段前处理平均/P50/P95 由 1.520/1.507/1.546 ms 降至 0.836/0.816/0.872 ms，平均下降 45.0%；300/300 帧的目标框、类别、置信度及其他输出元数据一致，NPU 推理和后处理耗时无实质变化。
- 五路开启同样函数剖析的对照中，tensor padding 单次 CPU 由 1278 μs 降至 593 μs（-53.6%），完整 DMA 前处理由 1788 μs 降至 1095 μs（-38.8%），进程 CPU 由 71.04% 降至 65.89%。优化后默认生产配置为 65.59%，原生产基线为 68.19%；接收保持约 135.03 帧/s，algorithm 推理保持约 36.13 次/s。
- 优化后默认生产进程在约 7、20、31 分钟时 RSS 分别为 401,224、407,816、410,200 KiB，但约 35 分钟密集告警录像后 RSS/PSS 阶跃到 444,980/441,665 KiB（约 434.6/431.3 MiB），与原基线相当。增量几乎全部来自新扩展的单个匿名分配器 arena，日志同时显示 `Recorder start/finish`、cached packets 和 snapshot，不是 algorithm 共享库/剖析计数器增长。因此只结论为“本次修改无常驻内存回退”；minor faults/s 下降约 55.5% 说明 CPU 触页减少，不等同于长时 RSS 同比下降。
- 最终共享库 SHA-256 为 `a5bf99ec044fba5474bded2df5322e510fd7f0b96830a3588bf4c930b97b1103`，已部署到板端 algorithm 与 media_agent 目录；默认生产进程保持 `5/5/0/0`，测试窗口剖析日志为 0，应用/algorithm 错误关键字为 0。完整报告见 `docs/EIC7700边缘设备npu计算能力实测.md#algorithm-yolov8`，板端原始数据保存在 `/home/ubuntu/workspace/test/algorithm/perf/20260831_*`。

## 2026-08-28

### 1. PPE Safety EIC7700 反光衣严重漏检定位与修复

#### 根因与不可行路径

- 量化前基准固定使用 91 上原始权重
  `weights/train/ppe_safety/ppe_safety_detect_cls11_yolo26s_640.pt.original`，SHA-256 为
  `551820d500036b59771c341838939597725edebcb79aa8a803df04a8efeff62d`；未再把经过图改写的
  `.pt` 当作浮点基准。
- 原始 PT 在 `Clothing.mp4`、confidence=0.25 下输出 3378 个检测，其中 Person 1166、
  vest 1158、helmet 1052。旧生产 PT 为绕过 EsAAC 不支持的 PSA Attention MatMul，将两个
  Attention 替换成零初始化 1x1 Conv；其输出只有 2991 个，同类别 IoU>=0.5 的总体保留率
  仅 0.7395，Person 匹配 623、漏 543，说明主要精度损失在量化之前已经产生。
- 已验证“不改 Attention/导出图直接量化”路径：原始六输出 ONNX 的 FP16 编译在
  400x32x400 的 QK MatMul tiling 阶段失败；调整 mapper、SRAM、优化选项、DSP 路径，以及
  对 MatMul 做等价分块/K padding 均不能通过。INT16 可由 EsQuant 生成，但 EsAAC 仍崩溃或
  超时。因此 SDK 202606 下不能直接发布原始 Attention 的 FP16/INT16 板端模型。

#### 修复

- 新增 `scripts/eic7700/distill_yolo26_attention.py`：冻结其余网络，用原始 PT 作为 teacher，
  只训练两个可编译的 3x3 空间卷积 Attention 替代层。训练集由 300 张通用图片和
  `Clothing.mp4` 前半段采样 87 帧组成，通用验证集 60 张；视频后半段 433 帧只用于盲测，
  不参与训练或量化校准。正式蒸馏 PT SHA-256 为
  `28034c46050a1f89f38dc66c4c423da777a706ebe4bc2fa320439bc3cfa0b2b5`，同时完整保留
  `.pt.original`。
- `eic7700_pipeline.py` 为 YOLO26 自动插入带显式零 bias 的恒等 1x1 输入适配器；只将该
  DMA 边界量化为 INT8，所有学习算子保持 INT16。直接 INT16 输入不被板端 DMA 接受；无 bias
  的恒等 Conv 又会触发 EsAAC 在 INT8->INT16 BiasAdd 边界缺少 `auxAttr`，显式零 bias
  同时保持数学等价并解决编译失败。
- `yolov26_postprocessor.cpp` 不再按输出数组位置套用 `table.json` 的 scale，而是根据输出
  tensor 的 box/score 语义和分支解析 `cv2.*`/`cv3.*` scale。实际 EsAAC ofmap 顺序为
  `box0,score0,box1,score1,box2,score2`，量化表顺序为
  `box0,box1,box2,score0,score1,score2`；旧代码使 6 个输出中的 4 个使用错误 scale，是候选
  模型初次上板出现漏检和误检的直接原因。
- PPE Safety 默认工作阈值经盲测扫描从 0.25 调整为 0.15；评估工具新增视频帧区间、默认
  分辨率以及旧 PT JSONL 行序帧号兼容，确保训练视频前后半段严格隔离。

#### 验证与部署

- `Clothing.mp4` 后半段 433 帧，相对原始 `.pt.original` 0.25 参考、板端阈值 0.15：总体
  1857 个参考框中匹配 1680、漏 177、额外 233，保留率 0.9047、Precision 0.8782、F1
  0.8912、平均 IoU 0.9113；vest 保留率 0.9332、Precision 0.9360、F1 0.9346；Person
  保留率 0.8981、Precision 0.9381、F1 0.9177。
- 相对蒸馏 PT 0.20 参考，板端总体保留率 0.9399、Precision 0.8829、F1 0.9105；vest
  保留率 0.9452，Person 保留率 0.9689。完整 866 帧板端运行平均 22.90 FPS。
- 92 的 SDK 202606 容器内 algorithm 交叉编译通过；修正库 SHA-256 为
  `2b6125c3703ae9c4bdfaddb80041c4600037cca7aeb0c181304046d8f4be4d7a`。91 上模型与事件包
  已重新打包并通过全量 EIC7700 审计；正式 vest 事件包 SHA-256 为
  `8bba99c402e046b4ff41deb25964c166ecba99a85c9510875da7c9a8139223ca`，model 为
  `91490411fc85b4b50c4c6108442dbbbd8c2d5eaa297b9b77353f8915b591db4f`，量化表为
  `b579fa5dda036e17a06b8be3209112db07122d9e8f59e183ce28eca69e8e86ad`。
- 正式包已部署到 127 的平台 recordings 与 seed 目录，修正库已部署到 `/opt/media_agent`
  及 `/home/ubuntu/workspace/test`。使用平台正式包短测 60 帧成功，输出 51 个框
  （helmet 27、vest 24）。91 备份位于
  `evaluation/yolov8_det_quantization/work/ppe_clothing/formal_backup_20260828_092839`；127 备份
  位于 `/home/ubuntu/workspace/test/ppe_clothing_eval/backups/formal_20260828_093203`。
- 测试结束后 `smart-guard-edge.service` 为 `active`，真实平台 socket
  `/opt/smart-guard/run/media-agent/media_agent.sock` 存在并处于 LISTEN；模拟测试未遗留假
  socket。

### 2. 事件 C 接口 ABI/空指针防护与 RK3588 `strlen` 崩溃修复

#### 根因

- RK3588 回溯显示 `libcdky_event.so!event_process` 在 `__strlen_generic` 中崩溃。现有接口对
  真正的空字符串指针已有判空，但无法识别“非空、地址已失效”的指针。
- `event_object_t` 曾追加 COCO-17 关键点数组，`event_request_t` 也曾追加
  `config_path`、`event_interval_ms`。这两个结构体都以数组形式跨动态库边界传递；当
  `media_agent` 与 `libcdky_event.so` 来自不同版本时，调用方和库使用的数组步长不同，第二个
  目标或第二个事件的 `class_name/event_name` 会被解释成无效地址，随后构造字符串时崩在
  `strlen`。这也解释了问题只在部分 RK3588 部署和多目标/多事件场景暴露。

#### 修改

- `media_agent_event.h` 新增 `EVENT_API_ABI_VERSION`、`event_api_layout_t` 和
  `event_process_v2`。调用方传入 ABI 版本及 frame/object/request/alarm 各结构体尺寸；事件库
  在解引用任何数组前完成一致性校验，不一致返回 `-2` 并记录实际/期望尺寸。
- 保留原 `event_process` 符号用于旧调用方兼容；本仓库 `EventPostStep` 已切换为
  `event_process_v2`，后续 RK3588 重新编译时会自动启用保护。
- 两个处理入口都在参数检查前将 `out_alarms=nullptr`、`out_alarm_count=0`，错误返回不会把
  旧输出留给调用方；`camera_id/class_name/event_name/config_path` 的空指针分别按默认相机、
  空类别、跳过空事件和无请求配置处理。
- `EventPostStep` 不再把 protobuf 字符串直接跨接口传递：事件名、模型配置路径、检测类别名
  和相机 ID 都由当前调用栈或成员容器显式持有，且预留容量后再绑定 `c_str()`，消除容器移动
  或配置对象生命周期造成的悬空风险。

#### 验证与部署

- 新增事件 API 回归：伪造 `event_object_t` 尺寸不匹配时返回 `-2` 且输出清零；camera、类别、
  事件名均为 `nullptr` 时安全返回。92 的 SDK 202606 容器内 algorithm 和完整 media_agent
  均以 riscv64 目标编译安装通过；127 板端 `event_api_test` 完整通过。
- 新的 `media_agent` 与 `libcdky_event.so` 已成对部署到
  `/home/ubuntu/workspace/test/media_agent`，SHA-256 分别为
  `158719eb2a64aadf06b12d169e1696193b61339b9ff6cbf57e24d352074a5219` 和
  `7a99f546db3df4bbb465b16a734d03394eaa29c87cf4bac88c9581ee29bbccd9`；旧文件备份在
  `/home/ubuntu/workspace/test/backups/media_agent_before_event_abi_20260828_1358`。
- 替换后真实平台任务自动恢复，区域入侵、聚集、打架和火焰事件均产生告警，未出现
  `event_process_v2` ABI 拒绝或进程崩溃。`smart-guard-edge.service` 未重启，真实平台 socket
  `/opt/smart-guard/run/media-agent/media_agent.sock` 保持存在。

## 2026-08-27

### 1. 反光衣反例事件与按跟踪 ID 的单次入区报警

#### 目标

- `vest-detection` 从“检测到 vest 报警”改为“检测到人员后，在人员框内未找到 vest 时报警”。
- `target_detection` 和新的反光衣事件类型均可选择按 `tracker_id` 过滤：同一目标连续处于 ROI 内只报警一次，离开 ROI 后即使 ID 不变，再进入仍重新报警。

#### algorithm

- 新增独立事件类型 `nested_absence_detection`，通过 `primary_labels`、`secondary_labels` 配置两级标签。二级目标框中心位于一级目标框内即认为匹配；无匹配的一级目标进入与 `target_detection` 一致的滑动窗口，报警证据对象为一级目标。
- `target_detection` 与 `nested_absence_detection` 新增 `track_id_filter_enabled`。开启后按正数 `tracker_id` 独立维护窗口及入区生命周期；明确检测到目标在 ROI 外时立即重新武装，短时漏检继续使用 `confidence_grace_ms`，`tracker_id == 0` 不参与该过滤模式。
- 默认配置保持该开关关闭以兼容旧事件；`area-intrusion` 和 `vest-detection` 显式开启。
- `vest-detection` 改用 `primary_labels: ["Person"]` 与 `secondary_labels: ["vest"]`；`Person` 大小写与 `ppe_safety` 模型实际类别名一致。中文描述改为“未穿反光衣”。
- `event_api_test` 增加两级框内匹配、二级框在一级框外、无二级目标、滑动窗口、连续入区去重、不同 ID 独立报警和同 ID 离区重入等回归用例。

#### vision-pipeline

- `server/config/Event.yaml` 与 algorithm 配置保持一致。
- `scripts/eic7700/package.py` 的事件标签校验按事件类型执行：`target_detection` 校验 `target_labels`，`nested_absence_detection` 联合校验 `primary_labels` 和 `secondary_labels`，避免 `vest-detection` 平台包重新生成时被旧的单层标签约束拒绝。
- 变更已同步到 91 服务器 `/home/cdky/workspace/gitlab/vision-pipeline`，Python 语法与命令入口检查通过；本次未重新量化或生成模型包。

#### 构建、部署与验证

- 92 服务器 SDK `202606` 容器：`algorithm` 执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 通过。
- 同一容器：`media-agent` 执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 通过。
- 127 EIC7700 板端：RISC-V `event_api_test` 使用 algorithm 独立安装库及 media_agent 最终安装库分别运行通过。
- `media_agent`、`libcdky_event.so` 和 `config/Event.yaml` 已部署到 `/home/ubuntu/workspace/test/media_agent`；`ldd` 无缺失依赖，`--help` 可运行，5 秒启动/TERM 冒烟测试完成并正常退出。非 sudo 冒烟测试因既有目录权限无法写日志或连接平台 socket，程序自动回退控制台日志，不影响进程启动与优雅退出。
- 按任务说明，本次未以 EIC7700 `ppe_safety` 模型的 `Person` 检测效果作为深度验收项。

### 2. YOLOv8 检测模型 PT/EIC7700 量化精度损失评估

> 本事项按“基线评估 → 类别字典修复 → 未过滤输出与阈值分析 → 高置信度量化修复”的顺序推进；最终发布结论以最后一个阶段为准，0.15 阈值方案已被后续 INT16 量化方案取代。

#### 目标与统一口径

- 使用 91 服务器 `data/dataset/coco/images/val` 的完整 200 张带标签数据，在 PT 和
  EIC7700 最终 pkg 上执行同批样本评估；共有 1535 个 GT 框、77 个实际出现类别。
- 两端统一输入 512、候选导出置信度 0.001、NMS IoU 0.7、max detections 300。
- 使用 pycocotools 的 COCO bbox 口径计算 mAP50-95、mAP50、mAP75、APs/APm/APl、
  AR@1/10/100 和 ARs/ARm/ARl；另在生产置信度 0.55 下计算 P/R/F1。
- 无标签 `car.mp4` 共对齐 617 帧，在相同 0.25 置信度下按同类别、IoU 不低于 0.5
  一对一匹配，计算保留率、PT-only、Board-only、框 IoU 和置信度偏差。

#### vision-pipeline

- 扩展 `scripts/inference/test_yolov8.py`：`--source` 支持图片、目录、视频、RTSP
  和摄像头；增加 `--iou`、`--max-det`、`--frame-limit`、`--vid-stride`、
  `--output-dir`、`--jsonl`、`--save-txt` 等参数。
- PT 结果保存版本化 `predictions.jsonl` 和包含模型 SHA-256、推理参数、记录数的
  `run_metadata.json`；可同时保存带框图片或视频。
- 新增独立目录
  `evaluation/yolov8_det_quantization`，包含统一结果解析、COCOeval、固定阈值指标、
  无标签一致性、三栏图片/双栏视频渲染、报告生成、91/127 执行脚本和完整使用手册。
- 明确 JSONL 坐标契约：PT 为 `xyxy_pixels`，板端为 `xywh_normalized`，必须携带
  原图宽高。修复旧 `calculate/evaluate_results.py` 一类流程可能把板端归一化
  `[x,y,w,h]` 误当像素 `[x1,y1,x2,y2]` 的风险；本次未复用该旧结果。
- 版本化报告和证据保存在
  `evaluation/yolov8_det_quantization/reports/2026-08-27`；完整大文件保存在 91
  服务器同目录的 `work` 下。

#### algorithm

- `eic7700_stream_infer_test` 的图片、视频和 RTSP JSONL 增加 schema 版本、原图宽高、
  source_name、frame_index 和明确的 `xywh_normalized` 坐标格式。
- JSON 输出改用 nlohmann/json 构造，避免路径或类别名特殊字符破坏 JSONL。
- `--save-visuals` 在视频/RTSP 下默认生成
  `output-dir/visualized.h264`；图片目录继续保存逐图 JPEG。
- 板端实测确认 EIC7700 VPS 单帧矩形硬件上限为 8：8 框完整处理 617 帧，9 框返回
  `ES_VPS_Rectangle` 错误。因此默认值由 128 修正为 8，并对参数范围 0 至 8 做校验。
  完整候选框仍全部写入 JSONL，不受硬件可视化上限影响。

#### 实测结果

- PT 模型 SHA-256：
  `1f47a78bf100391c2a140b7ac73a1caae18c32779be7d310658112f7ac9aa78a`。
- EIC7700 pkg SHA-256：
  `88da955f240793e77fb83f0ed2fe1fe8a3c259612dd90698ea17dd6cbdee57e8`。
- 200 张带标签集：
  - PT：mAP50-95=0.4474，mAP50=0.6136，AP small=0.1594。
  - EIC7700：mAP50-95=0.0248，mAP50=0.0321，AP small=0.0071。
  - 差值分别为 -0.4226、-0.5815、-0.1524；生产阈值 recall 从 0.3954 降至
    0.1427，small recall 从 0.0694 降至 0.0184。
  - 0.25 阈值下 PT 输出覆盖 74 类，EIC7700 仅覆盖 4 类：person、car、bus、truck。
    匹配框平均 IoU 为 0.9232，但总体保留率仅 0.2936，说明框位置不是主要矛盾。
- `car.mp4`：617 帧全部对齐；EIC7700 相对 PT 总体框保留率 0.5048，小目标保留率
  为 0，PT-only 4966，Board-only 127，匹配框平均 IoU 0.9594。
- 结论：当前包并非只有小目标轻微精度损失，而是量化/编译/DSP 后处理链路存在整体
  类别召回塌缩，不满足发布条件。阈值调整不能恢复缺失类别；下一步应按相同样本分别
  检查量化 ONNX、EsAAC 三个 int16 输出头、144 通道的 DFL/class 排列、ofmap 顺序和
  DSP sigmoid/class 参数。

#### 构建与运行验证

- 92 服务器 SDK 202606 容器内
  `./scripts/build.sh --target riscv64 --jobs 16 --install` 交叉编译安装通过。
- 127 开发板带标签目录 200/200 张处理完成，平均 13.59 张/秒。
- 127 开发板 `car.mp4` 指标采集 617/617 帧完成，平均 56.89 FPS；8 框硬件可视化
  617/617 帧完成，平均 54.75 FPS，生成约 13 MB H.264。
- 91 服务器 PT 带标签推理 200/200 张完成，无标签视频 617/617 帧完成；COCOeval、
  逐框 CSV、200 张三栏图片、617 帧双栏 MP4 和中文报告全部生成。

#### 阶段补充：`class_names` 根因修复与同批数据复测

- 最终根因不是 NPU 只产生 4 类，也不是 DSP 只支持 4 类。旧
  `weights/eic7700/coco/coco_detect_eic7700_1_4.pkg` 的 `num_class=80`，但包内
  `class_names` 字典仅包含 `0/2/5/7`。运行时已按 80 类完成 NPU/DSP 解码，其他
  76 类在 `EdgeInfer` 公共结果边界因类别名为空而被过滤，所以观测结果固定只剩
  person、car、bus、truck。
- 问题来源为 `scripts/eic7700/package.py`：模型级包错误复用了 COCO 事件级公开
  类别裁剪规则。现已分离两类契约：
  - `weights/eic7700/<task>` 的源 JSON 和模型级 pkg 使用字符串类 ID 到标签名的
    字典，并完整包含 `0..num_class-1`，用于独立推理和量化精度评估；
  - `weights/eic7700_platform` 的事件级 pkg 同样使用字典格式，COCO 事件包仍只
    公开 `0/2/5/7`，保持既有 media_agent 事件输出范围。
- `platforms/eic7700/quantize/eic7700_pipeline.py` 从生成源头改为输出字典格式；当前
  `weights/eic7700` 下 10 个模型类型的运行时 JSON 已全部转换为完整字典。
- `scripts/eic7700/audit.py` 新增强制回归：源 JSON 必须是完整字典；模型级 pkg
  必须包含全量 ID；事件级 pkg 独立检查允许公开 ID，防止两套规则再次混用。
- 91 服务器已重新生成全部 10 个模型级包和 21 个可用事件级包，并通过全量审计。
  新 COCO 模型级 pkg SHA-256 为
  `2924d94f0b81a296d836a8e1497ddf26258792048406400b2c224ad5ce93e8e8`；包内
  `class_names` 为字典且含 80 个键 `0..79`。COCO 平台事件包仍为字典键
  `0/2/5/7`，同源事件包摘要一致。
- 新 COCO 包已部署到 127 板端，并复用原 200 张 val 与 617 帧 car.mp4 重跑：
  - EIC7700 在置信度 0.25 下的类别覆盖由 4 类恢复至 73 类，PT 为 74 类；
  - EIC7700 mAP50-95 从 0.0248 恢复至 0.3900，mAP50 从 0.0321 恢复至
    0.5349，AP small 从 0.0071 恢复至 0.1176；
  - 相对 PT 的真实剩余差值为 mAP50-95 `-0.0574`、mAP50 `-0.0787`、
    AP small `-0.0418`；一致性保留率由 0.2936 提升至 0.7267，小目标保留率由
    0.2427 提升至 0.5097；
  - car.mp4 的结果不变：场景在比较阈值下本来就主要只有 person/car/bus/truck，
    因而不能单独暴露标签裁剪缺陷，也不能代替带标签多类别数据集评估。
- 修复后板端 200/200 张完成，平均 12.92 img/s；car.mp4 617/617 帧完成，平均
  56.34 FPS；`smart-guard-edge` 测试后已恢复为 active。
- 报告、机器可读指标、逐框差异 CSV 与更新后的对比图片保存在
  `evaluation/yolov8_det_quantization/reports/2026-08-27`；91 的完整结果分别位于
  `work/analysis_labeled_full_labels` 与 `work/analysis_car_full_labels`。

#### 阶段补充：`car.mp4` 未过滤输出修复、阈值扫描与小目标优化

- 继续检查旧 `car.mp4` 结果发现，`run_board.sh car` 虽传入
  `--confidence 0.001 --iou-thresh 0.7 --topk 300`，但旧
  `eic7700_stream_infer_test` 仅在图片模式调用 `prepare_evaluation_configs()`；视频和
  RTSP 直接用 pkg 初始化。结果是命令行 confidence 只在最终列表再次过滤，DSP 已按
  pkg 内 `conf_thresh=0.55` 不可逆地删除低分候选，旧 JSONL 不能代表未过滤输出。
- `algorithm/eic7700Infer/tests/eic7700_stream_infer_test.cpp` 已统一图片、目录、视频和
  RTSP：先解包模型、解析实际路径、把 confidence/IoU/topk 写入临时运行配置，再初始化
  `EdgeInfer`。帮助文本同步明确三个参数均覆盖模型后处理配置。
- 92 服务器 SDK `202606` 容器重新执行 algorithm 交叉编译安装成功；新测试程序 SHA-256
  为 `9d903ece161bb5136360f3a8f336eb998843761bddd401ff9d556b1d8932ba09`，已部署到
  127 的 `/home/ubuntu/workspace/test/algorithm/bin/eic7700_stream_infer_test`。
- 修复后以 confidence `0.001`、NMS IoU `0.7`、topk `300` 重跑 617 帧，板端导出
  123045 个候选（旧结果仅 5190 个）。固定 PT 参考 confidence=0.25 后：
  - 板端阈值 0.001：总体保留率 0.9788，小目标 55/55、保留率 1.0000；
  - 板端阈值 0.15：总体保留率 0.9095，小目标保留率 0.8182；
  - 板端阈值 0.25：总体保留率 0.8427，小目标保留率 0.6000；
  - 原阈值 0.55：总体保留率 0.5048，小目标保留率 0。
- 新增 `analyze_consistency_thresholds.py`，固定 PT 参考集合后扫描板端阈值，并输出总体、
  small/medium/large 的保留率、一致性 Precision/F1、IoU 和置信度偏移 JSON/CSV；新增
  `analyze_operating_thresholds.py`，用真实 YOLO 标签分别扫描 PT 和板端 P/R/F1。
- 200 张有标签集的板端阈值扫描显示：总体 F1 在 0.20 至 0.25 附近最高；小目标优先时
  0.15 的 small recall=0.2143、small F1=0.2796，优于 0.25 的 0.1633/0.2556。
  结合 `car.mp4` 小目标保留率从 0.6000 提升至 0.8182，本次将通用 COCO 模型级 pkg
  默认 `conf_thresh` 从 0.55 校准为 0.15。事件级 pkg 仍使用 Event.yaml 中各事件的
  业务阈值，没有随模型级评估包改变。
- `configs/eic7700_pipeline.json` 为 coco 固化 `conf_thresh=0.15`；
  `scripts/eic7700/quantize.sh` 现在把 manifest 中每个模型的 conf/iou/topk 传给量化配置
  生成步骤，避免重新量化时退回全局默认 0.55。
- 最终通用 COCO pkg SHA-256 为
  `b5d7efabf9752f374fdad6a245423469da47f017bc9308dbc0ae5cb7894d8fe9`，包内
  `class_names` 是完整字典 `0..79`，`conf_thresh=0.15`。无命令行阈值覆盖的板端默认
  pkg 回归完整处理 617 帧、平均 56.59 FPS；相对 PT 0.25 参考集合总体保留率 0.9072、
  小目标保留率 0.8182。
- COCO 标准 AP 仍使用 0.001 未过滤候选计算，未通过工作阈值掩盖量化误差：PT/板端
  mAP50-95 分别为 0.4474/0.3900，mAP50 为 0.6136/0.5349，AP small 为
  0.1594/0.1176。阈值优化解决的是置信度偏移导致的部署漏检，不会改变这些 AP 结论。
- 完整 JSONL、阈值扫描和指标保存在 91
  `/home/cdky/workspace/gitlab/vision-pipeline/evaluation/yolov8_det_quantization/work`；可提交的
  报告与 JSON/CSV 证据保存在 `reports/2026-08-27`。板端测试结束后
  `smart-guard-edge` 已恢复为 `active`。

#### 阶段补充：YOLOv8 检测高置信度量化根因修复（取代 0.15 阈值方案）

- 前一节将模型级 pkg 的生产阈值从 0.55 调到 0.15，只能绕过置信度下偏，不能修复量化
  数值误差。本节正式取代该发布方案：通用 COCO 模型级 JSON/pkg 已恢复
  `conf_thresh=0.55`，低阈值只保留为 AP 导出和诊断手段。
- EsQuant 精度分析显示，旧 INT8 的单层 reset cosine 约 0.9999，但 graphwise cosine
  随网络逐层累积，最差在 `/model.6/m.1/cv2/conv/Conv` 附近降至 0.82559。匹配框 IoU
  较高但分类置信度整体负偏，小目标的弱分类响应最先跌破 0.55，这是高阈值保留率下降的
  根本原因；输入预处理、类别映射和 DSP 阈值覆盖问题均已独立排除。
- `platforms/eic7700/quantize/eic7700_pipeline.py` 新增全局 `--quantized-dtype`、
  `--nodes-i8`、`--nodes-fp16`、optimization/bias 选项及精度节点互斥校验；默认校准来源改为
  train-only，禁止默认混用 val。`run_all.sh` 和 Docker 包装同步透传这些参数，并修复容器
  `/workspace` 默认误挂载到 quantize 子目录的问题。
- 正式 COCO 方案使用 train 200 张校准、全局 INT16，输入与第一个 Conv 保持 INT8 兼容
  VPS，三个检测输出头为 INT16，校准采用 `mse_eic` 和默认 requant `mean`。
- 同一 COCO val 200 张：mAP50-95 `0.389957 -> 0.397005`，mAP50
  `0.534868 -> 0.547527`，AP small `0.117624 -> 0.143335`；PT 分别为
  `0.447394/0.613566/0.159446`。
- PT 与板端同为 0.55：有标签总体保留率 `0.791483 -> 0.813510`，小目标
  `0.486486 -> 0.594595`，置信度偏差 `-0.027257 -> -0.011025`；`car.mp4` 总体
  `0.823949 -> 0.866910`，small `0/2 -> 2/2`，medium `0.697434 -> 0.805043`，
  置信度偏差 `-0.013286 -> +0.002264`。所有改进均未降低 0.55。
- FP16 主干图可以量化，但 SDK 202606 EsAAC 在 `AllocateMemoryPass.cc:354` 因 BiasAdd
  缺少 `auxAttr` 编译失败；`requant_mode=max` 对 car 保留率仅提高约 0.00055，均未采用。
- 新 INT16 正式包以包内默认 0.55 直测 `car.mp4` 617/617 帧，平均 34.11 FPS
  （实验运行为 29.92 FPS）、约 25 MB；仍存在 PTQ 残差。若验收门限更严格，应扩充
  train 校准集到约 1000 张并覆盖小目标/密集/逆光分布，仍不足时采用 QAT，禁止再次用
  低生产阈值代偿。
- 正式 pkg SHA-256：
  `aff967d6ee2e6af01aeceabb65a9587bc044f826d7352680fcc6ab661f2931ae`；model：
  `cd646cfcce0730ca7684f610dc751c2d6efb3775eda676f8934d3f963cc2a802`；量化表：
  `a9fdf6f18080c29ad63deaae8d1a1bbd1a015d8b96f248b7713e36812f6fed12`。
- 7 个依赖 COCO 的事件包已用同一 INT16 模型重新打包并部署，统一 SHA-256 为
  `1b99145efc759f56ce95addf121025144988be06c6fa0560f4bbdcb7c3abfded`；91 全量模型级与
  事件级包审计通过，127 测试后 `smart-guard-edge` 已恢复为 `active`。
- 完整报告与机器可读指标保存在
  `evaluation/yolov8_det_quantization/reports/2026-08-27`；91 完整运行目录为
  `work/analysis_labeled_train_i16_mse` 和 `work/analysis_car_train_i16_mse_same055`。

### 3. 区域入侵跟踪 ID 漏报、重复报警与多目标告警节流修复

#### 根因

- ByteTrack 用历史模型的固定类别编号 `3/4/5` 选择 person/motorbike 初始化阈值，无法适配
  COCO `person=0` 等新模型类别表；默认初始化阈值又高于 media_agent 实际保留检测结果的
  0.25 阈值，导致 0.25～0.5 的持续目标始终无法建立轨迹、`tracker_id` 长时间为 0。
- ByteTrack 已完成检测框与轨迹的内部匹配后，`MediaAgentTracker` 又用固定 IoU 0.3 做第二次
  贪心匹配。密集目标中第二次匹配可能丢失或错配内部已经确定的 ID。
- 事件模块把“同 ID 已检测到但置信度暂时低于 ROI 阈值”当作“目标已离开 ROI”，立即清除
  已报警锁存；置信度恢复后同一 ID 会再次报警。
- 跟踪过滤分支没有调用全局 `event_interval_ms`，多个目标同时满足窗口时可在连续帧快速报警。

#### 修改

- 跟踪初始化完全移除历史 `3/4/5` 类别分支，统一使用模型无关的通用阈值；旧的
  person/motorbike 字段只保留接口兼容。media_agent 明确将 tracker 的
  `min/high/person/motorbike` 阈值与推理输出最低保留阈值 0.25 对齐，持续低分目标仍需满足
  `n_init=3` 才确认。`EsTracker.yaml` 和内置默认值同步为 0.25。
- ByteTrack 在内部关联完成后直接返回每个 detection 的准确 track ID，删除上层第二次 IoU
  重匹配，避免密集目标中 ID 丢失或串配。
- `target_detection` 与 `nested_absence_detection` 将 ROI 几何关系和置信度判断分离：同 ID
  在区域内发生置信度抖动时不累计有效帧，但保留当前入区及已报警状态；明确检测到 ROI 外
  立即重新武装；完全漏检超过新增 `track_exit_grace_ms`（默认 2000 ms）才视为离区。
- 跟踪过滤事件的所有 ID 共用一个 `event_interval_ms`。每次间隔最多按 ID 顺序消费一个当前
  帧仍有效且滑动窗口仍满足的目标；等待期间消失或窗口失效的 ID 不延迟补报。
- 修复事件时间戳倒退时无符号减法溢出的边界行为。

#### 验证与部署

- 新增回归覆盖：class_id=0、置信度 0.30 的目标在 0.25 阈值下确认并保持准确 ID；同 ID
  区域内低置信度后恢复不重复报警；显式离区再进入可重新报警；ID 1/2/3 同时就绪时按
  500 ms 间隔依次消费，第三个 ID 在轮到前消失则不报警。EIC7700 板端
  `tracker_api_test`、`event_api_test` 均通过。
- 92 服务器 SDK 202606 容器内，algorithm 与 media_agent 均按
  `./scripts/build.sh --target riscv64 --jobs 16 --install` 交叉编译安装通过。
- 127 板端使用 91 的循环 `car.mp4` 做完整链路测试，最终版本日志确认 tracker 以
  `min_thresh=0.25 high_thresh=0.25` 初始化；区域入侵报警从 22:02:52 起严格按约 5 秒输出，
  告警 ID 依次为 4、35、62、86、120、167、186、210、246、267 等，同一连续入区 ID
  未重复消费。
- 对长视频 P1.mp4 未等待完整 1960 秒，而是直接使用 91 已循环推送的当前有效片段进行约
  4 分钟验证；人员目标在 20 秒内建立正 ID 并产生告警，后续仅在仍满足窗口时按 5 秒或
  更长间隔输出。最终 0.25 跟踪阈值版本又做了约 30 秒短片段回归，告警 ID 为 2、3、9、11，
  未出现全程 ID 0 导致的完全漏报。
- 最终部署文件 SHA-256：`media_agent`
  `6157c989435bb9616d4abc7cd3bda11ab4295b668a7fb1d7516b8c1221f0ce61`；
  `libcdky_track.so`
  `73c12db08d7237701038e41d4e9fee0830e0b4cdbb555be8e47e721be492f279`；
  `libcdky_event.so`
  `d8414fb322997b54e67cc725bacd9e99837c90336a5e9066ad5cf9687e5ed902`。
- 部署前版本已备份到 127：
  `/home/ubuntu/workspace/test/backups/media-agent-track-interval-20260827-2148`。

## 2026-08-26

### 1. EIC7700 YOLOv8 Pose 七类行为事件

#### 目标

依据 `proj/media-agent/third_party/algorithm/算法研发进度表.xlsx` 的“事件支持”页，实现 YOLOv8 Pose COCO-17 相关事件，并按事件编码生成独立 EIC7700 平台模型包。事件包括：

- `people-falling`
- `abnormal-posture`
- `fight-detection`
- `help-gesture`
- `posture-state`
- `prolonged-lying`
- `dangerous-zone-posture`

#### algorithm

- 扩展 `event_object_t`，增加固定容量的 COCO-17 关键点数组与数量字段；内部 `ObjectMeta` 同步携带关键点。
- EIC7700 兼容推理接口将 typed Pose 结果转换为旧 `object_result` 时保留 17 个关键点，避免关键点在兼容层丢失。
- 新增通用 `pose_behavior` detector。所有 Pose 事件共享关键点校验、人体尺度归一化、姿态分类、跟踪状态和时序窗口，通过 YAML `behavior` 选择具体规则，新增同类事件不再需要复制工厂分支。
- 摔倒采用“站立前态 + 躯干转水平 + 髋部下降 + 倒地持续”的时序规则；静态躺卧不直接判为摔倒。
- 异常姿态、姿态状态和危险区域动作采用躯干角度、膝关节角度、髋膝相对位置和框宽高比联合判断，并进行持续时间确认。
- 举手求助采用手腕相对肩部的归一化高度与持续时间判断。
- 长时间倒地增加持续躺卧及人体中心稳定性判断。
- 打架采用多人接近、肢体归一化速度、肢体到对方躯干接近三类线索及持续时间联合判断；当前为 Pose 时序启发式实现，为后续专用时序模型保留同一事件编码和替换边界。
- 危险区域动作默认要求存在有效 ROI；无 ROI 时拒绝触发，防止全画面误报。
- `config/Event.yaml` 增加 `event_type_defaults.pose_behavior` 和七个事件配置，关键点阈值、姿态阈值、时间窗、位移、多人交互等参数均可配置。
- `Event.yaml` 已为所有新增 Pose 公共字段和事件专用字段补充行内注释，明确单位、归一化方式、坐标方向、合法取值及触发语义。
- 单元测试增加七类 Pose 事件的正向和关键时序覆盖。

#### media-agent

- `EventPostStep` 根据 IoU 将原始 Pose 推理对象和跟踪后对象匹配，把 COCO-17 关键点及帧时间戳送入事件模块；跟踪 ID 继续用于跨帧状态管理。
- `tests/server` 增加七个事件编码到模型包路径的映射。
- 模拟器增加 `--events`、`--rtsp-url`、`--output-rtsp-url` 和 `--model-root` 参数，可从命令行构造单路、多事件测试任务，无需修改 Go 源码。
- 新增 `proj/media-agent/docs/Pose事件模拟任务下发与板端验证.md`，记录模型打包、Go 模拟器构建、板端部署、单/多事件任务下发、危险区 ROI 和验收过程。

#### vision-pipeline

- `configs/eic7700_pipeline.json` 将七个事件映射到 `yolov8_pose` 模型源。
- `scripts/eic7700/package.py` 在生成事件包时合并 `event_type_defaults` 与事件级配置，使继承的 `target_labels` 也能正确参与打包。
- 生成以下 `1_4` 事件包：

```text
weights/eic7700_platform/people-falling/people-falling_eic7700_1_4.pkg
weights/eic7700_platform/abnormal-posture/abnormal-posture_eic7700_1_4.pkg
weights/eic7700_platform/fight-detection/fight-detection_eic7700_1_4.pkg
weights/eic7700_platform/help-gesture/help-gesture_eic7700_1_4.pkg
weights/eic7700_platform/posture-state/posture-state_eic7700_1_4.pkg
weights/eic7700_platform/prolonged-lying/prolonged-lying_eic7700_1_4.pkg
weights/eic7700_platform/dangerous-zone-posture/dangerous-zone-posture_eic7700_1_4.pkg
```

- 七个事件包复用相同 Pose 模型；同一次打包中的七个包摘要一致。加入最终事件配置并重新打包后，91 服务器七个包的 SHA-256 均为 `9e92b64ec363b2893110cb8a97fdb0cde1502a4e53f119005d7c2d169c2aa6b3`。
- `docs/use.md` 更新 EIC7700 Pose 事件映射和平台事件数量。

#### 验证结果

- 92 服务器、SDK `202606` 容器：`algorithm` 执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 通过。
- 同一容器：`media-agent` 执行 `./scripts/build.sh --target riscv64 --jobs 16 --install` 通过，验证新关键点 ABI 与主工程集成。
- 92 服务器容器原生构建：`ctest --output-on-failure` 共 3 项全部通过，其中 `event_api_test` 覆盖七类 Pose 事件。
- 91 服务器 Go 工具链：`make server` 和 `go test ./server ./proto ./cmd/server` 通过；新增的事件编码解析/去重/非法输入测试通过，并成功生成静态链接的 RISC-V `server_riscv64`。
- 模拟任务服务、protobuf、事件配置及验证文档已统一放置在 91 服务器 `/home/cdky/workspace/gitlab/vision-pipeline/server`。
- 本地 `proj/media-agent/third_party/vision-pipeline/server` 同步保存同一套可版本化源码；`bin/` 和生成的 `pb/*.pb.go` 由 `.gitignore` 排除。
- `vision-pipeline`：七个事件包生成成功；`scripts/eic7700/audit.py` 全量审计通过。`mask-detection`、`smoking-detection` 仍按既有规则标记为无可发布权重，与本次变更无关。
- EIC7700 板端真实 RTSP 的首轮验证结果见下方“板端告警修复与实测补充”；后续仍应使用现场摄像头样本持续标定阈值。

#### 阶段补充：板端告警修复与实测补充

- 根因定位：事件层此前直接丢弃 `tracker_id == 0` 的 Pose 对象；主跟踪器阈值高于事件阈值，且遮挡场景会发生 ID 跳变，导致时序条件无法累计。条件偶发失败时也未使用 `confidence_grace_ms`，单帧漏点会立即清零。
- `pose_behavior` 增加内部人体空间关联：优先复用外部跟踪 ID，未确认或 ID 跳变时通过框 IoU 与人体尺度归一化中心距离继承状态。新增 `pose_match_iou_threshold`、`pose_match_center_distance_ratio`，均已在 `Event.yaml` 注释单位与含义。
- 所有单人 Pose 行为使用 `confidence_grace_ms` 容忍短时漏检；打架的交互对状态同样保留宽限窗口。跌倒转变被短时锁存，在倒地确认阶段不再要求每一帧都重新找到站立历史。
- 关键点特征允许单侧肩或髋被遮挡，只要有效关键点总数达标；降低侧视、遮挡场景漏报。
- 下蹲由原来的“膝角或髋膝距离”改为“膝角/髋膝距离 + 腿部收缩 + 人体框垂直压缩”联合判断，新增 `crouching_max_leg_extension_ratio`、`crouching_min_aspect_ratio`，消除交叉腿正常站姿误报。
- 事件告警日志增加姿态、躯干角、框宽高比、腿伸展率和举手数，便于现场阈值标定。
- 单元回归新增：空跟踪 ID、ID 跳变、短时漏点、跌倒锁存、单侧肩遮挡、交叉腿负样本和真实下蹲正样本；EIC7700 板端 `event_api_test` 全部通过。
- 真实链路验证：91 模拟任务服务向 127 板端下发任务，七类事件均经 Pose 推理和事件模块产生 `MSG_ALARM`。正常直立单人负样本连续 30 秒无告警。
- 测试码流兼容性：原始 1080p H.264 直接复制会使板端 VDEC 返回 `mem_err=1`；转为 1280x720、Constrained Baseline、无 B 帧、单参考帧后解码与推理稳定。相关 FFmpeg 参数和日志目录权限处理已写入 `vision-pipeline/server/Pose事件模拟任务下发与板端验证.md`。
- 最终验证：92 的 SDK `202606` 容器中 `algorithm` 与 `media-agent` 交叉编译安装通过；板端七类正样本告警通过，正常站姿负样本通过。
- 91 服务器按最终 `server/config/Event.yaml` 重新生成七个事件包并完成全量审计；最终包已同步到 127 板端，使用新包复测 `prolonged-lying` 告警通过，任务心跳显示 `success=1 failed=0`。
