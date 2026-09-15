# media_agent 多模型同类目标重复告警修复与验证记录

## 1. 问题现象

EIC7700 板端同一通道同时配置多个模型，且多个模型均输出 `person` 时，`area-intrusion` 已开启 `track_id_filter_enabled: true`，仍可能对看起来相同的 `track_id` 重复告警。现场记录中，同一通道的 `track_id=4719` 在约 30 秒内重复触发，期间没有任务重建、配置更新或 RTSP 重连。

## 2. 根因

问题由三个相互叠加的状态边界造成：

1. EIC7700 和 RK3588 的旧兼容推理结果只保留框、类别和置信度，模型来源在扁平化时丢失。上层因而把同一通道全部模型的目标送入同一个 ByteTrack，也把全部目标送给每个事件检测器。
2. ByteTrack 旧匹配按历史“大类”而非模型内精确 `class_id` 限制。同一通道多个模型都输出 `person` 时，跨模型框可能竞争或复用轨迹，造成 ID 和事件输入不稳定。
3. `TrackPostStep` 在零目标帧直接返回，ByteTrack 的 `max_age` 没有推进；事件层却在目标缺失超过 `track_exit_grace_ms` 后清除“本次入区已报警”状态。目标再次出现时，ByteTrack 可能继续给出原 ID，而事件层已把它当作新入区，形成同 ID 重复报警。

单独修改 algorithm 无法可靠解决第一项：algorithm 推理层知道每个结果来自哪个模型，但旧 media_agent 中间结构会删除该信息。仅做跨模型 NMS 会误删不同任务的有效框，仅永久记忆历史 ID 又会使真实离区重入无法再次报警。因此采用“media_agent 最小来源透传和路由 + algorithm 跟踪/事件生命周期修复”。平台 protobuf、任务协议、告警结构和模型输出内容均不变。

## 3. 实现方案

### 3.1 模型来源透传

- EIC7700 `object_result` 从 typed `ModelResult` 保留 `model_key`、原始配置/包路径和全部别名。
- RK3588 `edgeDeploy` 为每个模型结果填入初始化时的模型路径；同步补齐 Pose 字段，保持 media_agent 两个平台共用后处理源码可编译。
- `FmtInferPostStep` 在进程内保存与输出框严格按下标对齐的来源元数据和原始结果索引。该数据不会写入平台 protobuf。

### 3.2 按模型独立跟踪和事件路由

- `TrackPostStep` 由“每路流一个 tracker”调整为“每路流、每个模型一个 tracker”。相同模型内容键仍共享一个 tracker，不同模型不再竞争轨迹。
- 即使某模型当前帧没有检测框，也继续调用其 tracker，使 `max_age` 正常推进。
- `EventPostStep` 按任务中的 `model_config_name` 与推理结果来源/别名匹配；每个事件只接收自身模型的目标和存活轨迹集合。
- Pose 关键点通过保存的原始结果索引精确回填，取消跨模型 IoU 猜测匹配。
- 只有一个事件且后端没有来源元数据时保留旧路由兼容；多事件场景不把未知来源目标广播给所有事件。

### 3.3 跟踪和事件生命周期

- ByteTrack 匹配改为模型内精确 `class_id`，防止不同类别共用 ID。
- 新增 `tracker_list_live_track_ids()`，返回尚未删除的已确认轨迹；短暂漏检且未超过 `max_age` 的轨迹仍属于存活集合。
- `event_frame_desc_t` 增加完整存活轨迹集合。`target_detection` 和 `nested_absence_detection` 在集合可用时，仅在以下情况重新武装：明确观察到目标位于 ROI 外，或 tracker 确认删除 ID。旧调用方没有生命周期集合时继续使用 `track_exit_grace_ms` 回退逻辑。
- 状态删除日志明确记录 `explicit_outside`、`tracker_deleted`、`missing_timeout` 或 `event_reset`，便于区分真实重入、轨迹淘汰和兼容超时。

## 4. 兼容性与影响范围

- 不修改平台下发协议、告警上报 protobuf、类别名称、置信度、ROI 或事件阈值。
- 单模型通道保持原有输出；新增来源字段只在进程内参与分组和路由。
- `yolov8_det`、`yolov26_det` 和 `yolov8_pose` 使用各自独立 tracker；`yolov8_cls` 不产生目标跟踪事件时不受影响。
- `event_frame_desc_t` 增加字段，直接调用 `cdky_event` 的程序必须与新版头文件一起重新编译。未填写 `has_live_tracker_ids` 的新编译调用方仍使用旧超时策略。
- 本次没有修改 `Event.yaml`，已有事件开关、标签和阈值保持不变。

## 5. 构建与验证

### 5.1 交叉编译

在 92 服务器 `cross-proj-202606-cdky` 容器、SDK 202606 环境中，以 `-j4` 限制并发完成：

- algorithm 的 `es_bytetrack`、`cdky_track`、`es_eventedge`、`cdky_event`、`cdky_eic7700_infer`、`tracker_api_test`、`event_api_test` 目标编译；
- media_agent RISC-V 全量对象重编译及最终可执行文件链接。

服务器源码树已有 `eic7700Infer_save`、`eventEdge_save`、`tests_save` 备份目录。原 algorithm 顶层 CMake 使用 `*/CMakeLists.txt` 通配扫描，任何包含 CMake 文件的备份或实验目录都会被当作正式模块并重复声明 target。现已改为 `byteTrack`、`eic7700Infer`、`eventEdge`、`tests` 固定白名单；其他任意命名目录都不会隐式进入构建，无需依赖后缀排除，也没有删除、移动或修改备份目录。

白名单修改后使用标准命令 `./scripts/build.sh --target riscv64 --jobs 4 --install` 重新配置、编译并安装成功。配置输出只包含四个白名单模块，未加载三个 `_save` 目录。

### 5.2 EIC7700 板端回归

测试程序和候选库放在 `/home/ubuntu/workspace/test/algorithm/tmp_event_fix_validation_20260903`，通过临时 `LD_LIBRARY_PATH` 运行，没有替换生产 media_agent 或 algorithm 安装目录：

- `tracker_api_test`：PASS。覆盖空帧推进、短暂漏检期间存活 ID 保留、不同 `class_id` 不共用轨迹。
- `event_api_test`：PASS。覆盖告警后目标缺失超过 2 秒但 tracker 仍持有 ID 时不重复报警，以及 tracker 确认删除后相同 ID 重新建立窗口并允许再次报警。

本次只完成代码、构建和定向回归，未重启板端生产进程，未替换生产二进制和共享库。正式部署后还应使用同一通道多模型真实任务观察状态清理原因日志，并确认同一连续入区生命周期只上报一次。
