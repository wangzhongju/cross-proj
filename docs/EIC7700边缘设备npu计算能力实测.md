# EIC7700 边缘设备 NPU 计算能力实测

## 1. 结论摘要

2026-09-11 在 EIC7700 开发板 `192.168.88.127` 上完成同一 YOLOv8s、640×640、
batch=1、INT8 模型的多路径对比。`pipeline` 不是自研流水线，而是
`pipeline_20260630` 的官方历史版本，并在其上叠加了与 Media Agent 大致相同的业务代码；
`pipeline_20260630` 是本次使用的官方基线版本，尚未叠加业务代码。饱和场景下，官方基线
无显示 OD 为 78.575 FPS，官方历史版本的十路业务链路为 75.862 FPS，相差 2.713 FPS
（3.45%）；`es_run_model` 纯模型为 78.2318 FPS。

Media Agent 原实现因相同 pkg 被合并到单个串行 `ModelExecutor`，即使增加到三路也只有
52.5 FPS、NPU 忙时约 66.8%。将已有 `npu_max_inflight` 配置落实为三路同模型 executor 池后，
三路完整业务流在 60.008 秒内完成 4506 次推理，即 75.089 FPS、2.148 等效 TOPS，驱动
NPU busy 为 96.620%。由此可见当前模型在该编译器和运行时上的稳定上限约为 75～79 FPS。

按 ONNX 稠密卷积、MAC=2 OPS 统计，本模型为 28.602624 GOP/帧。实测最高模型等效算力
为 2.249 TOPS，即 13.3 TOPS 标称 INT8 峰值的 16.91%。这里的“模型等效 TOPS”只计
卷积乘加，不计激活、Resize、EDMA、DSP、访存和调度；NPU 驱动忙时约 99.9%，因此不能将
16.91% 解读为还有 83% 的可直接使用算力。

量化精度在工程冻结的 200 张 COCO 验证集上完成全量逐图评估：PT mAP50:95 为
46.707%，EIC7700 INT8 为 46.527%，下降 0.181 个百分点；mAP50 从 63.716% 变为
63.886%，上升 0.171 个百分点。该结果是项目 200 图验证集结果，不冒充 COCO val2017
官方 5000 图指标。

区域入侵 `1_6` 事件包已发布并部署，公开类别为 `person(0)`、`car(2)`：

- 91 服务器：`weights/eic7700_platform/area-intrusion/area-intrusion_eic7700_1_6.pkg`
- 127 开发板：`/home/ubuntu/workspace/test/models/eic7700_platform/area-intrusion/area-intrusion_eic7700_1_6.pkg`
- SHA-256：`05c238d380f8418b188a3367ec2bbd9cee4b5bc965b9cad21b7b02416e6a3863`

## 2. 模型与统一测试口径

模型使用 Ultralytics 标准 YOLOv8s COCO 80 类权重。源文件名仍含历史的 `512`，但本次
ONNX 导出、校准、编译和运行均显式固定为 640×640，不能按文件名误判输入尺寸。

| 项目 | 固定值 |
| --- | --- |
| PT SHA-256 | `1f47a78bf100391c2a140b7ac73a1caae18c32779be7d310658112f7ac9aa78a` |
| 编译 `.model` SHA-256 | `2af2c4f2b14c59df9cb0f3c7d10db791f53ff1a8c087cd4f4bd1998c7bb9d664` |
| 输入 | RGB、NCHW、1×3×640×640、等比例缩放、114 填充 |
| 量化 | 全局 INT8，三个 YOLOv8 输出头保留 INT16 |
| 后处理 | confidence=0.001、IoU=0.7、topk=300 |
| 运算量 | 28,602,624,000 OPS/帧，稠密 Conv，MAC=2 OPS |
| 理想上限 | 13.3 TOPS ÷ 28.602624 GOP = 464.992 FPS |

四条路径均加载上述同一个 `.model`。Algorithm 精度评估使用保留 80 类的模型级
`coco_detect_eic7700_1_6.pkg`；Media Agent 和官方历史版本 Pipeline 使用裁剪公开标签后的区域入侵
事件包。两种 pkg 内的 `.model` 和量化表哈希相同，事件包只改变运行时公开类别。

## 3. 量化、编译与精度损失

### 3.1 执行过程

流程遵循 `proj/media-agent/third_party/vision-pipeline/docs/pt_to_eic7700_deployment.md`
的既有路径，没有新增量化脚本：

1. 在 91 服务器导出三头 YOLOv8 ONNX，输出形状为 `[1,144,80,80]`、
   `[1,144,40,40]`、`[1,144,20,20]`。
2. 使用 COCO train 子集 200 张做 MSE、per-channel INT8 校准，20 张做逐层误差分析。
3. EsQuant 全局 `quantized_dtype=int8`，无 `nodes_i8/nodes_i16/nodes_fp16` 例外；三个外部
   输出头指定为 INT16。EsAAC 编译成功并生成 14,433,400 字节模型。
4. 使用同一 200 张带标注 val 图片，91 GPU 生成 PT JSONL，127 板端生成 INT8 JSONL，
   两端统一 confidence=0.001、IoU=0.7、topk=300，再以 COCOeval 计算完整 AP/AR。

20 张误差分析的累计输出头余弦相似度依次为 99.9545%、99.9220%、99.9234%。逐层重置
分析的输出头接近 100%，说明单层量化误差很小；最终是否可用仍以板端逐图 AP/AR 为准。

### 3.2 精度结果

| 指标 | PT FP32 | EIC7700 INT8 | 板端减 PT |
| --- | ---: | ---: | ---: |
| mAP50:95 | 46.707% | 46.527% | -0.181 pp |
| mAP50 | 63.716% | 63.886% | +0.171 pp |
| mAP75 | 49.282% | 50.030% | +0.747 pp |
| AP small | 19.780% | 19.606% | -0.175 pp |
| AP medium | 49.600% | 47.646% | -1.954 pp |
| AP large | 63.609% | 63.603% | -0.005 pp |
| AR@100 | 62.095% | 61.537% | -0.558 pp |
| person AP50:95 | 57.700% | 56.465% | -1.235 pp |
| car AP50:95 | 50.790% | 49.627% | -1.163 pp |

在 confidence=0.25、匹配 IoU=0.5 的 PT/板端一致性检查中，PT 1355 个框有 1162 个匹配，
保留率 85.756%，匹配框平均 IoU 为 94.812%，板端置信度相对 PT 的平均偏移为 -0.02125。
在固定 confidence=0.55 时，PT/板端 F1 分别为 58.076%/55.496%。区域入侵实际告警阈值
由平台任务控制，不能直接用 AP 导出的 0.001 作为生产阈值。

## 4. 性能结果

| 路径 | 场景 | FPS | 平均时延 | 等效 TOPS | 13.3 TOPS 占比 |
| --- | --- | ---: | ---: | ---: | ---: |
| `es_run_model` | 100 预热 + 1000 次纯模型 | 78.232 | 12.7825 ms | 2.238 | 16.82% |
| Algorithm `npu` | 预处理一次，60 秒纯 runtime | 76.779 | 13.0235 ms | 2.196 | 16.51% |
| Algorithm `pipeline` | DMA 预处理 + NPU + DSP，60 秒 | 54.762 | 18.2595 ms | 1.566 | 11.78% |
| 官方 Pipeline `infer` | 无显示异步链，1 路无帧率限制 | 78.635 | 12.7021 ms/忙时 | 2.249 | 16.91% |
| 官方 Pipeline `od` | 上述链路 + DetectionOut | 78.575 | 12.7108 ms/忙时 | 2.247 | 16.90% |
| Media Agent | 1 路 25 FPS 完整业务流 | 24.995 | 12.6901 ms/NPU 忙时 | 0.715 | 5.38% |
| Media Agent | 3 路同源 100 FPS、3 executor 完整业务流 | 75.089 | 12.8674 ms/NPU 忙时 | 2.148 | 16.15% |
| 官方历史版本 Pipeline + 业务代码 | 1 路 25 FPS、每 3 帧推理 | 7.724 | — | 0.221 | 1.66% |
| 官方历史版本 Pipeline + 业务代码 | 10 路 25 FPS、每 3 帧推理 | 75.862 | — | 2.170 | 16.31% |

`es_run_model`、Algorithm NPU 和官方 Pipeline 的差异在约 2% 内。官方 Pipeline 使用异步
流水和驱动完成计数，结果略高于 `es_run_model` 0.52%，属于测量边界和调度差异，不能据此
断言媒体 Pipeline 的单次 NPU 延迟低于官方模型工具。

官方历史版本 Pipeline 的十路业务链路稳态 29 个采样点为 75～76 FPS，平均 NPU 占用
99.551%，相对官方基线 Pipeline OD 低 3.45%。运行阶段无 critical error，10 路均启动；
停止阶段 45 秒内仍有 8 个
worker 未退出，脚本因此按设计判失败，随后清理检查时进程已全部退出。吞吐结果可用，但这次
退出超时必须作为独立稳定性问题保留，不能标记为完整通过。

Media Agent 单路连续 60.013 秒完成 1500 次推理，为 24.995 FPS；NPU 忙时 31.719%，
业务日志持续显示接收、解码、推理和发布均约 25 FPS。该结果受输入源限制，仅证明单路实时
能力，不是 Media Agent 的饱和容量上限。

### 4.1 Algorithm `npu` 与 `pipeline` 的差别及 54.762 FPS 根因

两种模式不是同一计时边界：

- `npu` 在进入 60 秒计时前只做一次 DMA 预处理，循环内仅执行 `Eic7700Infer::run` 和输出
  buffer 归还，因此测的是准备好输入张量后的 runtime 吞吐；
- `pipeline` 每一轮都调用 `EdgeInfer::infer`，同步包含 VPS/DMA 预处理、NPU runtime、DSP
  `DetectionOut` 后处理、结果转换与过滤，因此测的是单线程完整算法链时延。

驱动数据证明降速不在 NPU 本身：`npu` 与 `pipeline` 每次推理的 NPU busy 分别为
12.6882 ms 和 12.6892 ms，几乎完全相同；但完整链平均时延从 13.0235 ms 增至
18.2595 ms，多出 5.2360 ms，NPU busy 占测量墙钟也由 97.420% 降至 69.488%。在同一
200 图板端结果中，分段耗时均值为预处理 2.7998 ms、NPU 13.1338 ms、后处理 3.8616 ms；
因检测框数量随图片变化，该 200 图均值不直接替代固定图片 benchmark，但足以与约 5.2 ms
额外时延互证。结论是 54.762 FPS 来自同步前后处理造成的 NPU 空窗，不是模型在 NPU 上变慢。

### 4.2 官方 Pipeline `infer` 与 `od` 的差别

二者都不是 `es_run_model` 那种纯模型测试，均包含 Demux、VDEC、Mux、Queue、PreProcess、
EsInfer 和无显示 TestSink。`infer` 在 EsInfer 后直接进入 TestSink，输出原始张量；`od` 则在
EsInfer 后增加 EsPostProcess，调用 DSP `ES_AK_DSP_DetectionOut` 完成 YOLOv8 DFL 解码、
置信度筛选和 NMS，再进入 TestSink。

官方流水线用 Queue 异步重叠各阶段，瓶颈仍是 NPU，因此本次 `infer`/`od` 分别为
78.635/78.575 FPS，差值仅 0.061 FPS（0.077%）。这表示 DSP 后处理被流水线隐藏，并不表示
DetectionOut 没有耗时；要看真正的纯推理基线，应使用 `es_run_model`。

### 4.3 Media Agent 饱和压测与修复

单路 100 FPS 输入能稳定接收、解码和发布 100 FPS，但原实现只推理约 44～46 FPS；两路和
三路同模型分别都停在约 52.5 FPS。源码定位到 `ModelService` 以 pkg SHA-256 复用唯一
`ModelExecutor`，而 executor 的单 worker 把预处理、NPU、DSP 后处理整段串行，导致增加流数
也无法填补 NPU 空窗。

修复后 `npu_max_inflight=3` 同时控制同模型 executor 池和设备级在途任务上限，与 Media Agent
三个推理线程匹配。模拟任务服务支持 `--events area-intrusion -s 3`，三路复用同一个 100 FPS
输入、加载同一个 `area-intrusion_eic7700_1_6.pkg`，输出 URL 自动编号。最终 60.008 秒窗口：

| 指标 | 结果 |
| --- | ---: |
| 驱动完成次数 | 4506 |
| 驱动计数 FPS | 75.089 |
| 业务日志稳态 infer | 74.6～75.2 FPS |
| NPU busy | 57.980 秒 / 60.008 秒 = 96.620% |
| 等效 TOPS | 2.148 |
| 13.3 TOPS 占比 | 16.149% |

三路在测量窗口内持续为 `3/3/0/0`，没有推理失败，停止后 Media Agent 与模拟服务均正常退出。
100 FPS 输入仅用于消除输入供给不足，最终吞吐按 NPU 驱动完成次数计算，不把接收、解码或发布
的 300 FPS 当成模型 FPS。

## 5. 官方 Pipeline 无显示压测实现

新增 `case/od/od_headless_pressure.sh`，只装配 Demux、VDEC、Mux、PreProcess、Infer、
可选 PostProcess 和 TestSink。脚本会检查 NPU 独占，按运行时 JSON 自动设置 640 输入、
YOLOv8 DetectionOut、80 类和三头 scale，并保存以下证据：

- `command.txt` 与 `SHA256SUMS`；
- 复制后的实际 YAML 配置；
- `pipeline.log` 和周期硬件采样；
- 基于 `/proc/esnpu/stat` 的 `summary.json`。

首次板端试跑发现官方 `EsPostProcess.yaml` 同时存在检测与分类两个 `labelfile-path`，脚本原先
错误要求字段只能出现一次。修正为“至少出现一次并统一替换”后，`infer` 与 `od` 两种 60 秒
测试均通过。使用方法已经写入 `case/run.md`。

## 6. es_run_model 流程与工具差异

完整步骤位于 `pipeline_20260630/case/es_run_model/README.md`。板端工具 `-s` 并不接受
自定义文件名，而是在当前目录固定生成 `model_perf_data.json`；README 已按实测行为处理。
正式结果为 min=12.7280 ms、max=14.7800 ms、avg=12.7825 ms、78.2318 FPS。

官方工具不包含 Runtime 集成代码、媒体前处理和 DSP 后处理，适合做编译模型基线；不能用它
替代 Media Agent 或 Pipeline 的业务验收。

## 7. 发布、部署与复现位置

模型级包：

- 91：`weights/eic7700_yolov8s_int8/coco/coco_detect_eic7700_1_6.pkg`
- SHA-256：`4c79b188bc5ae9507484b15130aea8f4836929233f444b8361e7a05e8fa08687`

区域入侵事件包解包核对为 `model_type=yolov8_det`、`input=640×640`、`num_class=80`，公开
`class_names={0: person, 2: car}`，内部模型 SHA 与性能测试一致。模拟平台仍固定请求 `1_4`
文件名，因此 Media Agent 隔离测试目录使用 `1_4 -> 1_6` 软链接；正式发布目录只新增真实
`1_6` 文件，不覆盖既有 `1_5`。

本地证据根目录为 `outputs/diagnostics/yolov8s-int8-20260911/`：

- `benchmark_summary.json`：统一机器可读结果；
- `board/results/`：四类板端原始日志、驱动计数与运行配置；
- `quant-eval/evaluation/yolov8s_int8_20260911/analysis/metrics.json`：完整指标；
- `quant-eval/weights/eic7700_yolov8s_int8/coco/esquant/`：量化配置与余弦结果；
- `quant-eval/logs/eic7700_verify/coco_yolov8s_int8_quantize_20260911.log`：量化编译原始日志。

交叉编译验证在 92 服务器 SDK 202606 容器完成：Algorithm 使用 `--build-tests --install`
成功，测试程序 SHA-256 为 `841f7013de9274623996f34dd43fa7a174116d38c423c6a4d29635e63cb88d7b`；
`pipeline_20260630` 使用标准 `--target riscv64 --jobs 16 --install` 成功。完成 executor 池修改后，
Algorithm 与 Media Agent 又分别执行标准 riscv64 install 增量构建并成功。板端部署前版本备份于
`/home/ubuntu/workspace/test/backups/media_agent_before_executor_pool_20260911/`。最终检查板端没有
残留 Media Agent、Pipeline、`es_run_model` 或 Algorithm 测试进程。

## 8. 结论边界与后续建议

本次可以确认：同一模型下，官方历史版本 Pipeline 叠加业务代码后的饱和吞吐接近官方基线
Pipeline，差距约 3.45%；Algorithm 的纯 NPU 封装开销很小，完整同步前后处理使单线程吞吐
下降约 28.7%；Media Agent 修复同模型单 executor 串行瓶颈后达到 75.089 FPS、NPU busy
96.620%。13.3 TOPS 是特定算子、数据布局和芯片条件下的理论峰值，
当前通用 YOLOv8s 图只能达到约 2.25 模型等效 TOPS。

后续若要进一步逼近峰值，应优先做编译图逐算子分析、批处理/多核并行、EDMA 与激活融合及
不同卷积形状的 microbenchmark，而不是继续增加 RTSP 路数。精度发布前若需要对外声明 COCO
官方指标，应另行在 val2017 全量 5000 张上复测；本报告的 200 图结果只用于本项目量化前后
同口径比较。
