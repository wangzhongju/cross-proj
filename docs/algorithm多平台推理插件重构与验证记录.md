# algorithm 多平台推理插件重构与验证记录

## 1. 任务范围

日期：2026-08-24。

本轮只重构 `proj/media-agent/third_party/algorithm`。`media_agent` 的任务管理、VDEC、录像、推流、事件模块均未修改。设计遵循：

- 总体架构参考《媒体代理及算法模块完整目标架构设计》；
- EIC7700 SYS/VPS/NPU/DSP 生命周期和内存区域参考 `pipeline_20260630`；
- 兼容现有 `EdgeInfer` 调用，新增平台无关接口供后续 RK3588、NVIDIA 接入；
- 已验证模型扩展为 YOLOv8 detect、YOLOv26 detect、YOLOv8 pose、YOLOv8 classification；
- face/plate 只预留强类型结果和插件名，不虚报运行能力。

## 2. 落地架构

```text
                   稳定公共层（不含平台 SDK 类型）
┌─────────────────────────────────────────────────────────────┐
│ algorithm::api::IInferenceBackend                           │
│ FrameView / InferenceRequest / BackendCapabilities          │
│ ModelResult = Detection | Pose | ClassScore | Face | Plate  │
└─────────────────────────────┬───────────────────────────────┘
                              │
                    create_eic7700_backend()
                              │
┌─────────────────────────────v───────────────────────────────┐
│ EdgeInfer facade -> ModelService -> shared ModelExecutor    │
│                                     每模型一个 SDK worker    │
└─────────────────────────────┬───────────────────────────────┘
                              │
                     ModelPluginRegistry
          ┌───────────────────┼────────────────────┐
          v                   v                    v
 yolov8_detect        yolov26_detect        yolov8_pose / cls
          └───────────────────┬────────────────────┘
                              v
              Eic7700ModelPluginBase
       VPS prepare -> NPU run -> family postprocess
```

公共头文件：

- `include/algorithm/result_schema.h`：平台无关的 detect/pose/cls/face/plate 结果；
- `include/algorithm/inference_backend.h`：统一帧、请求、能力和 backend 接口；
- `include/algorithm/backends/eic7700_backend.h`：EIC7700 工厂函数。

EIC7700 平台适配位于 `eic7700Infer/platform/eic7700`。当前 capability 只报告 Detection、Pose、Classification；RK3588/NVIDIA 只在公共枚举和扩展点中预留，没有伪实现。

## 3. 模型插件拆分

模型插件位于：

```text
eic7700Infer/model/plugins/
  detection/                 # detect 插件公共适配，不包含模型族算法
  yolov8_detect/             # YOLOv8 detect 工厂
  yolov26_detect/            # YOLOv26 detect 工厂
  yolov8_pose/               # YOLOv8 pose 工厂
  yolov8_classification/     # YOLOv8 cls 工厂
```

后处理位于 `eic7700Infer/postProcess`：

- `yolov8_postprocessor.*`；
- `yolov26_postprocessor.*`；
- `yolov8_pose_postprocessor.*`；
- `yolov8_classification_postprocessor.*`；
- `tensor_decode_utils.*` 只保存量化 tensor 读取、sigmoid、坐标和 IoU 等无状态工具。

`Eic7700ModelPluginBase` 负责公共硬件阶段，不了解 YOLO 输出布局。各插件只做自身 ABI 校验和后处理，从而避免继续把 YOLOv26、pose、cls 堆入同一个 `yolov8_det` 实现。

规范模型类型为：

| 规范名称 | 旧 pkg 兼容别名 |
|---|---|
| `yolov8_detect` | `yolov8_det`、`yolov8s_detect` |
| `yolov26_detect` | `yolov26_det`、`yolo26_det` |
| `yolov8_pose` | 无 |
| `yolov8_cls` | 无 |

旧名称只在注册表入口转换，运行 executor 使用规范名称。

## 4. 前处理共享

每个插件公开 `PreprocessSignature`。签名覆盖 device、tensor shape/dtype/layout/pixel format/buffer size、resize/crop、比例保持、normalize、量化 scale、padding、mean/std。同一 `EdgeInfer::infer()` 调用内使用帧级缓存：

1. 首个签名执行 VPS 并生成引用计数 `PreparedInput`；
2. 后续签名完全相同的模型复用该 DMA tensor；
3. 每个模型仍有独立 NPU 和后处理；
4. 调用结束后释放缓存，生命周期不越过源 VDEC 帧租约。

板测中 YOLOv26 detect 与 YOLOv8 pose 均为 640×640 且前处理参数一致，因此每帧命中一次共享；512×512 YOLOv8 detect 和 224×224 center-crop classification 分别执行自己的 VPS 前处理。

## 5. EIC7700 SDK 对齐

模型打开顺序：

```text
SYS/VPS preprocessor open -> NPU model open -> ABI validate
```

关闭顺序严格反向：

```text
NPU model close -> VPS preprocessor close -> SYS/VB 引用释放
```

DSP 路径与 `pipeline_20260630` 一致：

```text
ES_AK_Init -> ES_AK_SetDevice(dspID) -> ES_AK_DSP_DetectionOut -> ES_AK_Deinit
```

DSP 输出/count 使用持久 VB block，MMZ 为 `mmz_nid_<die>_part_0`。隔离测试必须携带 `config/EsInfer.yaml`；若缺失会退回内置 DSP0，与生产使用 DSP3 的配置不同，不能作为有效验证环境。

## 6. 图片测试性能修复

旧图片路径的主要开销是：逐模型建立检测器、同一图片重复读取/解码、每模型重复硬件前处理，以及默认写多张标注图。

新路径：

```text
cv::imread 一次
  -> BGR 转 NV12 一次
  -> 构造一个 64-byte stride 对齐的 DMA surface
  -> EdgeInfer::infer（所有模型）
  -> typed JSONL
  -> 仅 --save-visuals 时写一张合并标注图
```

EIC7700 SDK 202606 的 VPS NV12 源帧要求宽 stride 64 字节对齐；高度按 32 对齐。测试 surface 使用 `mmz_nid_0_part_0`，不能使用非法父区名 `mmz_nid_0`。

默认不写 JPG，因此图片精度评估不会被磁盘编码吞吐主导。需要人工查看时显式增加 `--save-visuals`。

## 7. 交叉编译验证

92 交叉编译容器执行：

```bash
cd /workspace/proj/media-agent/third_party/algorithm
./scripts/build.sh --target riscv64 --jobs 16 --install
```

结果：`cdky_eic7700_infer`、`eic7700_stream_infer_test`、`eic7700_edgeinfer_api_test` 全部编译和链接成功，公共 `include/algorithm` 已安装到 `build-riscv64/install/include/algorithm`。

## 8. 板端验证

测试前按用户要求停止原 `media_agent`，确认 `/proc/esmap/dec` 无活动 group，`/proc/eswin/vb` 中原 PID owner 数量为 0。测试产物部署到隔离目录：

```text
/home/ubuntu/workspace/test/algorithm_refactor
```

使用的四个既有 pkg：

- `coco_detect_eic7700_2_0.pkg`：YOLOv8 detect；
- `ppe_safety_detect_eic7700_1_1.pkg`：YOLOv26 detect；
- `yolov8_pose_pose_eic7700_1_2.pkg`：YOLOv8 pose；
- `yolov8_cls_cls_eic7700_1_2.pkg`：YOLOv8 classification。

### 8.1 四模型单图

结果：成功，4 个类型化结果均返回；单帧四模型硬件执行约 85 ms，`shared_preprocess_hits=1`。总进程时间还包含四个 pkg 解包和模型初始化，不能用于计算稳态单帧 FPS。

### 8.2 14 图目录

结果：

```text
processed_images=14
model_runs=56
shared_preprocess_hits=14
elapsed=5.62479s
avg_model_image_fps=9.95592
JSON_LINES=56
VISUAL_FILES=0
```

14 张图片分辨率不同，全部通过 VPS/NPU/后处理。`VISUAL_FILES=0` 证明默认路径没有隐式写标注图。

### 8.3 统一 API 类型校验

环境变量同时传入四个 pkg 后执行 `eic7700_edgeinfer_api_test`，返回：

```text
API_RET=0
models=4
Detection items=7
Detection items=12
Pose items=6, shared_preprocess=true
Classification items=100
```

测试同时检查 `ModelTaskType` 与 `std::variant` payload 匹配，避免把 pose/cls 误塞进 detection 容器。

最终规范化注册表改动再次于 92 服务器完整交叉编译，并在 127 板端覆盖隔离目录中的
推理库和测试程序后复测。四个 pkg 仍使用历史 `yolov8_det`/`yolov26_det` 等包内名称，
因此本次成功同时验证了“旧包别名 -> 规范插件名称”的兼容路径。测试结束后确认：

```text
media_agent process count = 0
eic7700 test process count = 0
/proc/esmap/dec active group = 0
/proc/eswin/vb test owner = 0
```

本轮按要求没有恢复业务进程，避免隔离测试结束后擅自改变板端运行状态。

## 9. 当前限制

- 当前同一帧不同模型按稳定顺序执行 NPU，但可共享完全相同的 VPS tensor；尚未实现跨模型异步 NPU DAG。
- face/plate 只有 schema 和注册占位，没有模型 ABI 和板端验证。
- RK3588/NVIDIA 只有公共 backend 扩展点，尚未把 `edgeDeploy` 或 TensorRT 实现迁入。
- classification 的类别数、top-k 和概率语义由 pkg JSON 决定；测试工具不擅自覆盖模型契约。
- 本轮未重启 `media_agent`。后续集成测试应由使用者决定何时恢复业务进程。

## 10. 2026-08-24 图片几何与统一 infer 接口修复

### 10.1 bbox 偏移根因

图片测试的 DMA allocation 使用 64 字节宽 stride、32 行高 stride，这是正确的
物理内存约束；但旧代码把对齐后的 stride 同时填入 `image.width/image.height`，使
VPS 把 padding 当成可见图像。另一方面，letterbox 的 `dstRect` 曾额外执行偶数
截断，后处理却仍使用截断前的浮点 scale/pad。两处几何不一致共同造成框偏移。

修复后遵循真实 VDEC surface 和 `pipeline_20260630` 的语义：

```text
width/height               = 可见图像尺寸
width_stride/height_stride = DMA 物理布局
postprocess transform      = 实际提交给 VPS 的整数 dstRect
```

hardhat 20 图、`confidence=0.65` 对比：修复前输出 65 个目标，修复后输出 73 个；
首图此前遗漏的 person 恢复，hat/person 框与原图目标位置一致。`confidence=0.25`
修复后输出 104 个目标，其中归一化面积小于 0.002 的小目标为 39 个。

### 10.2 小目标漏检根因

`--confidence` 同时覆盖 pkg 的 `conf_thresh` 并执行最终结果过滤。相同修复版程序：

| 阈值 | 总目标 | 面积小于 0.005 | 面积小于 0.002 |
|---:|---:|---:|---:|
| 0.65 | 73 | 27 | 10 |
| 0.25 | 104 | 57 | 39 |

因此大量小目标消失的主要原因是 `0.65` 过高，而不是 NPU、类别白名单或 NMS
异常。精度/召回验证建议从 0.25～0.40 开始；生产阈值应以验证集 PR 曲线确定。

### 10.3 API 收敛

公开的多模型专用入口已移除，所有调用统一为 `EdgeInfer::infer()`：

- `vector<object_result>` 重载保持 media_agent 现有 detect 兼容；
- `vector<ModelResult>` 重载返回 detect/pose/cls 及未来 face/plate 类型化结果；
- EIC7700 backend、stream test 和 API test 均改用类型化 `infer` 重载。

板端动态符号检查仅存在三个 `EdgeInfer::infer` 重载，安装头文件不再包含旧入口。

### 10.4 完整 install 板测

92 服务器重新交叉编译后，将整个 `build-riscv64/install` 部署为：

```text
/home/ubuntu/workspace/test/algorithm
```

验证结果：

| 模型 | 图片数 | JSONL 行数 | payload 总数 | 结果 |
|---|---:|---:|---:|---|
| hardhat YOLOv8 detect | 20 | 20 | 73（阈值 0.65） | 通过 |
| PPE YOLOv26 detect | 41 | 41 | 212 | 通过 |
| YOLOv8 pose | 20 | 20 | 48 | 通过 |
| YOLOv8 classification | 20 | 20 | 2000（top-k=100/图） | 通过 |

四个 pkg 同时执行 `eic7700_edgeinfer_api_test` 返回 0，类型化 `infer` 共返回四个
ModelResult。测试结束后 `media_agent` 仍保持停止，`/proc/esmap/dec` 无活动组。
随后在 92 容器中执行完整 `media_agent` 的 riscv64 `--install` 构建，主程序编译、
链接和安装成功，证明旧的 detect 兼容重载无需修改上层调用即可与新库链接。
