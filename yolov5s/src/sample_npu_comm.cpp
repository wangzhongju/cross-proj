// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <unistd.h>
#include <fstream>
#include <algorithm>
#include <es-numa.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include "sample_npu_comm.h"
#include "sample_npu_utils.h"

#define SAMPLE_NPU_DEBUG(ret, msg, ...) \
    printf("[%s, %d, Ret: 0x%x]: " msg "\n", __FUNCTION__, __LINE__, ret, ##__VA_ARGS__)

ES_S32 SAMPLE_NPU_COMM_InitDevice(ES_U16 devId) { return ES_NPU_SetDevice(devId); }

ES_S32 SAMPLE_NPU_COMM_ReleaseDevice(ES_U16 devId) { return ES_NPU_ReleaseDevice(devId); }

ES_S32 SAMPLE_NPU_COMM_ConstructTask(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> &imgPath,
                                     std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> process,
                                     NPU_TASK_S &task, std::vector<std::vector<std::string>> &pictures)
{
    ES_S32 ret = 0;
    ES_S32 numInputs = 0;
    ES_S32 numOutputs = 0;
    ES_S32 inputTensorId = 0;
    ES_S32 outputTensorId = 0;
    NPU_TENSOR_S tensor;
    ES_U8 *pData = nullptr;

    // 获取输入 Tensor 数量
    ret = ES_NPU_GetNumInputTensors(modelId, &numInputs);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetNumInputTensors failed");
        return ret;
    }

    if (numInputs != imgPath.size()) {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "InputFileDir not equal numInputsTensors");
        return ES_FAILURE;
    }

    task.inputFdNum = numInputs;
    for (inputTensorId = 0; inputTensorId < numInputs; inputTensorId++) {
        // 获取输入 Tensor 描述
        ret = ES_NPU_GetInputTensorDesc(modelId, inputTensorId, &tensor);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetInputTensorDesc failed");
            goto input_failed;
        }

        // 根据输入 Tensor 描述分配 Tensor 内存
        ret = ES_SYS_MemAlloc(&task.inputFd[inputTensorId].memFd, SYS_CACHE_MODE_NOCACHE, "npu_sample",
                              devId == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0", tensor.bufferSize);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_SYS_MemAlloc failed");
            goto input_failed;
        }

        task.inputFd[inputTensorId].size = tensor.bufferSize;
        task.inputFd[inputTensorId].offset = 0;

        for (ES_S32 i = 0; i < tensor.dims.n; i++) {
            if (!imgPath[inputTensorId].empty()) {
                if (pictures.size() <= inputTensorId) {
                    std::vector<std::string> vec;
                    pictures.push_back(vec);
                }

                pictures[inputTensorId].push_back(imgPath[inputTensorId].front());

                // Read jepg image files
                std::vector<char> inFileData;
                ret = read_jpeg_data(imgPath[inputTensorId].front().c_str(), inFileData);
                if (ret != ES_SUCCESS) {
                    SAMPLE_NPU_DEBUG(ret, "Failed to read_jpeg_data:%s\n", imgPath[inputTensorId].front().c_str());
                    return ES_FAILURE;
                }

                // Decode jpeg image to B8G8R8
                JPEG_DATA jpegData;
                jpegData.data = reinterpret_cast<uint8_t *>(inFileData.data());
                jpegData.len = static_cast<uint32_t>(inFileData.size());

                if (esVdec->decodeJpeg(jpegData) != ES_SUCCESS) {
                    SAMPLE_NPU_DEBUG(ret, "Failed to decodeJpeg:%s\n", imgPath[inputTensorId].front().c_str());
                    return ES_FAILURE;
                }

                VDEC_VIDEO_FRAME frame;
                if (!esVdec->getVideoFrame(frame)) {
                    SAMPLE_NPU_DEBUG(ret, "Failed to getVideoFrame:%s\n", imgPath[inputTensorId].front().c_str());
                    return ES_FAILURE;
                }

                // do preprocess
                PREPROCESSED_DATA processedData;
                processedData.fd = task.inputFd[inputTensorId].memFd;  // taskMem.inputFd[inputTensorId].memFd;
                processedData.offset = task.inputFd[inputTensorId].offset;
                processedData.size = task.inputFd[inputTensorId].size;
                ret = process->preprocess(frame.videoInfoFrame, processedData);
                if (ret != ES_SUCCESS) {
                    SAMPLE_NPU_DEBUG(ret, "preprocess failed");
                    ES_SYS_MemFree(task.inputFd[inputTensorId].memFd);
                    esVdec->releaseFrame(frame);
                    goto input_failed;
                }

                // release video frame
                esVdec->releaseFrame(frame);
                imgPath[inputTensorId].erase(imgPath[inputTensorId].begin());
            } else {
                // If picture is not enough for the task, set it to zero. I don't if it is a good idea.
                // memset(pData + i * (tensor.bufferSize / tensor.dims.n), 0x00, tensor.bufferSize / tensor.dims.n);
            }
        }

        ret = ES_NPU_Prepare(&(task.inputFd[inputTensorId]));
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_Prepare input failed");
            goto input_failed;
        }
    }

    ret = ES_NPU_GetNumOutputTensors(modelId, &numOutputs);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetNumOutputTensors failed");
        goto output_failed;
    }

    task.outputFdNum = numOutputs;
    for (outputTensorId = 0; outputTensorId < numOutputs; outputTensorId++) {
        ret = ES_NPU_GetOutputTensorDesc(modelId, outputTensorId, &tensor);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetOutputTensorDesc failed");
            goto output_failed;
        }

        ret = ES_SYS_MemAlloc(&task.outputFd[outputTensorId].memFd, SYS_CACHE_MODE_NOCACHE, "npu_sample",
                              devId == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0", tensor.bufferSize);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_SYS_MemAlloc failed");
            goto output_failed;
        }

        task.outputFd[outputTensorId].size = tensor.bufferSize;
        task.outputFd[outputTensorId].offset = 0;

        ret = ES_NPU_Prepare(&task.outputFd[outputTensorId]);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_Prepare output failed");
            goto output_failed;
        }
    }

    return ES_SUCCESS;

output_failed:
    for (ES_S32 i = 0; i < outputTensorId; i++) {
        ES_SYS_MemFree(task.outputFd[i].memFd);
    }

input_failed:
    for (ES_S32 i = 0; i < inputTensorId; i++) {
        ES_SYS_MemFree(task.inputFd[i].memFd);
    }

    return ret;
}

ES_VOID SAMPLE_NPU_COMM_DestroyTask(NPU_TASK_S &task)
{
    for (ES_S32 i = 0; i < task.inputFdNum; i++) {
        ES_NPU_Unprepare(&task.inputFd[i]);
        ES_SYS_MemFree(task.inputFd[i].memFd);
    }

    for (ES_S32 i = 0; i < task.outputFdNum; i++) {
        ES_NPU_Unprepare(&task.outputFd[i]);
        ES_SYS_MemFree(task.outputFd[i].memFd);
    }
}

ES_VOID SAMPLE_NPU_COMM_DestroyUserTask(NPU_TASK_S &task, npu_stream stream)
{
    NPU_TASK_MEM_S taskMem;

    taskMem.inputFdNum = task.inputFdNum;
    taskMem.outputFdNum = task.outputFdNum;
    memcpy(taskMem.inputFd, task.inputFd, sizeof(task.inputFd));
    memcpy(taskMem.outputFd, task.outputFd, sizeof(task.outputFd));
    ES_NPU_ReleaseTaskMemory(task.modelId, 1, &taskMem);
}

ES_S32 SAMPLE_NPU_COMM_PostProcess(NPU_TASK_S &task, std::vector<std::string> &pictures, const std::string &outputDir,
                                   std::shared_ptr<EsPostProcess> postProcess, bool isFlexibleTask,
                                   uint64_t taskTotalCount)
{
    ES_S32 ret;
    NPU_COMPOSITE_MODEL_INFO_S compositeModelInfo;
    std::vector<ES_TENSOR_S> postTensors;
    ES_S32 processType = postProcess->getProcessType();
    ES_U32 modelId;

    if (isFlexibleTask) {
        ret = ES_NPU_GetCompositeModelInfo(task.modelId, &compositeModelInfo);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetCompositeModelInfo failed\n");
            return ret;
        }
        modelId = compositeModelInfo.modelsInfo[0].modelId;
        for (ES_S32 i = 0; i < compositeModelInfo.modelNums; i++) {
            if (compositeModelInfo.modelsInfo[i].modelType == MODEL_TYPE_NPU) {
                modelId = compositeModelInfo.modelsInfo[i].modelId;
            }
        }
    } else {
        modelId = task.modelId;
    }

    for (ES_S32 tensorId = 0; tensorId < task.outputFdNum; tensorId++) {
        NPU_TENSOR_S tensor;
        ES_TENSOR_S esTensor;

        ret = ES_NPU_GetOutputTensorDesc(modelId, tensorId, &tensor);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetOutputTensorDesc failed\n");
            return ret;
        }

        esTensor.pData = task.outputFd[tensorId];
        esTensor.dataType = postProcess->convertDataType(tensor.dataType);
        if (processType == EsPostProcess::CLASSIFY) {
            esTensor.shapeDim = 6;
        } else if (processType == EsPostProcess::DETECTION_OUT) {
            esTensor.shapeDim = 5;
        } else if (processType == EsPostProcess::RTMPOSE) {
            esTensor.shapeDim = 2;
        } else {
            esTensor.shapeDim = 5;
        }
        if (isFlexibleTask) {
            esTensor.shape[0] = 1;
        } else {
            esTensor.shape[0] = tensor.dims.n;
        }
        esTensor.shape[1] = tensor.dims.c;
        esTensor.shape[2] = tensor.dims.h;
        esTensor.shape[3] = tensor.dims.w;
        esTensor.shape[4] = 1;
        if (processType == EsPostProcess::CLASSIFY) {
            esTensor.shape[5] = esTensor.shape[4] * esTensor.shape[1];
        }
        postTensors.push_back(esTensor);
    }

    if (processType == EsPostProcess::DETECTION_OUT) {
        // Sort inTensor because of input_shape of post_process.json
        std::sort(postTensors.begin(), postTensors.end(),
                  [](ES_TENSOR_S &t1, ES_TENSOR_S &t2) { return t1.pData.size < t2.pData.size; });
    }

    if (postProcess->getProcessType() == EsPostProcess::CLASSIFY) {
        std::vector<std::vector<ES_S32>> topKIndex;
        std::vector<std::vector<float>> topKconfidence;
        ret = postProcess->classify(postTensors, topKIndex, topKconfidence);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "postProcess classify failed");
            return ES_FAILURE;
        }
        for (ES_S32 i = 0; i < pictures.size(); i++) {
            std::string label = postProcess->getLabelByIdx(topKIndex[i].front());
            printf("%s: lable(%d:%s), confidence(%f)\n", pictures[i].c_str(), topKIndex[i].front(), label.c_str(),
                   topKconfidence[i].front());
            if (!outputDir.empty()) {
                postProcess->writeClassesInfoToFile(pictures[i], topKIndex[i], topKconfidence[i], outputDir);
            }
        }
    } else if (postProcess->getProcessType() == EsPostProcess::DETECTION_OUT) {
        std::vector<std::vector<std::vector<float>>> boxInfo;
        ret = postProcess->detectionOut(postTensors, boxInfo);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "postProcess detectionOut failed");
            return ES_FAILURE;
        }
        for (ES_S32 i = 0; i < pictures.size(); i++) {
            printf("%s: box count(%d)\n", pictures[i].c_str(), boxInfo[i].size());
            for (auto &box : boxInfo[i]) {
                std::string label = postProcess->getLabelByIdx(int(box[1]));
                printf("class_id: %f(%s), score: %f, x_min: %f, y_min: %f, x_max: %f, y_max: %f\n", box[1],
                       label.c_str(), box[2], box[3], box[4], box[5], box[6]);
            }

            if (!outputDir.empty()) {
                postProcess->drawObjectToPicture(pictures[i], boxInfo[i], outputDir);
                postProcess->writeDetectResultToFile(pictures[i], boxInfo[i], outputDir, taskTotalCount);
            }
        }

    } else if (postProcess->getProcessType() == EsPostProcess::SEGMENT) {
        postProcess->segment(postTensors, nullptr);
    } else if (postProcess->getProcessType() == EsPostProcess::RTMPOSE) {
        std::vector<ES_POSE_POINT> poseResult;
        postProcess->rtmPose(postTensors, poseResult);
        if (!outputDir.empty()) {
            postProcess->drawPosePointToPicture(pictures[0], poseResult, outputDir);
            postProcess->writePosePointResultToFile(pictures[0], poseResult, outputDir, taskTotalCount);
        }
    } else {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "Not support postProcess type\n");
    }

    return ES_SUCCESS;
}

ES_VOID *SAMPLE_NPU_COMM_QueryThread(ES_VOID *args)
{
    ES_S32 ret;
    SampleStateInfo *info = (SampleStateInfo *)(args);
    pthread_setname_np(pthread_self(), "QueryThread");
    while (!info->queryThreadExited) {
        ret = ES_NPU_ProcessReport(info->stream, info->queryMs);
        if (ES_SUCCESS != ret) {
            SAMPLE_NPU_DEBUG(ret, "err:esProcessReport failed!break...\n");
            break;
        }
    }

    return nullptr;
}

ES_S32 SAMPLE_NPU_COMM_TaskCallback(ES_VOID *args)
{
    SampleCallbackArgs *sampleArgs = (SampleCallbackArgs *)args;
    ES_S32 ret;

    ret = SAMPLE_NPU_COMM_PostProcess(*sampleArgs->task, sampleArgs->pictures.front(), sampleArgs->outputDir,
                                      sampleArgs->postProcess, sampleArgs->userTask, sampleArgs->totalTask);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_PostProcess failed\n");
    }

    (*sampleArgs->completeNum)++;
    if (sampleArgs->userTask) {
        SAMPLE_NPU_COMM_DestroyUserTask(*sampleArgs->task, sampleArgs->stream);
    } else {
        SAMPLE_NPU_COMM_DestroyTask(*sampleArgs->task);
    }

    if (sampleArgs->task && sampleArgs->task->state == NPU_TASK_STATE_FAILED) {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "Task id:%d failed \n", sampleArgs->task->taskId);
    }

    delete sampleArgs->task;
    delete sampleArgs;

    return ES_SUCCESS;
}

ES_S32 SAMPLE_NPU_COMM_CreateQueryTrd(SampleStateInfo &info, npu_stream stream)
{
    info.queryThreadExited.store(false);
    info.taskNum = 0;
    info.queryMs = 100;
    info.stream = stream;
    info.queryThread = std::thread(SAMPLE_NPU_COMM_QueryThread, &info);

    return ES_SUCCESS;
}

ES_S32 SAMPLE_NPU_COMM_UserTaskCallback(ES_VOID *args) { return ES_SUCCESS; }

ES_S32 SAMPLE_NPU_COMM_SubmitAsync(ES_U16 devId, ES_U32 modelId, npu_stream stream,
                                   std::vector<std::vector<std::string>> inputFiles, const std::string &outputDir,
                                   std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                   std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret = ES_SUCCESS;
    std::atomic<ES_S32> completeNum(0);
    SampleStateInfo sampleInfo;
    // New thread to processReport
    sampleInfo.completeNum = &completeNum;
    SAMPLE_NPU_COMM_CreateQueryTrd(sampleInfo, stream);
    uint64_t taskTotalCount = inputFiles.front().size();

    while (!inputFiles.front().empty()) {
        NPU_TASK_S *task = nullptr;
        SampleCallbackArgs *callbackArgs = nullptr;
        task = new (std::nothrow) NPU_TASK_S;
        if (!task) {
            SAMPLE_NPU_DEBUG(ES_FAILURE, "new NPU_TASK_S failed, memory exception");
            ret = ES_FAILURE;
            goto thread_exited;
        }
        memset(task, 0x00, sizeof(NPU_TASK_S));
        callbackArgs = new (std::nothrow) SampleCallbackArgs;
        if (!callbackArgs) {
            SAMPLE_NPU_DEBUG(ES_FAILURE, "new SampleCallbackArgs failed, memory exception");
            ret = ES_FAILURE;
            delete task;
            goto thread_exited;
        }

        callbackArgs->completeNum = &completeNum;
        callbackArgs->postProcess = postProcess;
        callbackArgs->task = task;
        callbackArgs->userTask = false;
        callbackArgs->stream = stream;
        callbackArgs->outputDir = outputDir;
        callbackArgs->totalTask = taskTotalCount;
        ret = SAMPLE_NPU_COMM_ConstructTask(devId, modelId, inputFiles, esVdec, preProcess, *task,
                                            callbackArgs->pictures);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_ConstructTask failed");
            delete callbackArgs;
            delete task;
            goto thread_exited;
        }

        task->modelId = modelId;
        task->taskId = sampleInfo.taskNum++;
        task->callback = SAMPLE_NPU_COMM_TaskCallback;
        task->callbackArg = callbackArgs;

        ret = ES_NPU_SubmitAsync(task, 1, stream);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_SubmitAsync failed");
            SAMPLE_NPU_COMM_DestroyTask(*task);
            delete callbackArgs;
            delete task;
            goto thread_exited;
        }
    }

    // loop for check all tasks complete.
    while ((*sampleInfo.completeNum).load() != sampleInfo.taskNum.load()) {
        sleep(1);
    }

thread_exited:
    sampleInfo.queryThreadExited.store(true);
    if (sampleInfo.queryThread.joinable()) {
        sampleInfo.queryThread.join();
    }

    return ret;
}

ES_S32 SAMPLE_NPU_COMM_SubmitSync(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> inputFiles,
                                  const std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                                  std::shared_ptr<IPreprocess> preProcess, std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret = ES_SUCCESS;
    ES_U32 taskId = 0;
    double totalInferenceTime = 0.0;
    double totalPostProcessTime = 0.0;
    uint64_t taskCount = 0;
    uint64_t taskTotalCount = inputFiles.front().size();

    while (!inputFiles.front().empty()) {
        // NPU_TASK_S defined in /usr/include/essdk/es_npu_types.h:194
        // typedef struct {
        //     ES_U32 taskId;
        //     ES_U32 modelId;
        //     ES_U8 inputFdNum;
        //     ES_U8 outputFdNum;
        //     ES_DEV_BUF_S inputFd[ES_TASK_MAX_FD_CNT];
        //     ES_DEV_BUF_S outputFd[ES_TASK_MAX_FD_CNT];
        //     NPU_TaskCallback callback;
        //     ES_VOID *callbackArg;
        //     NPU_TASK_STATE_E state;
        //     ES_U8 sdkPrivate[ES_TASK_SDK_PRIVATE_LEN];
        // } NPU_TASK_S;
        NPU_TASK_S task;
        memset(&task, 0x00, sizeof(NPU_TASK_S));
        std::vector<std::vector<std::string>> pictures;
        // 构建 NPU 任务
        ret = SAMPLE_NPU_COMM_ConstructTask(devId, modelId, inputFiles, esVdec, preProcess, task, pictures);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_ConstructTask failed");
            return ret;
        }

        task.modelId = modelId;
        task.taskId = taskId++;
        task.callback = nullptr;  // 同步模式，无回调
        task.callbackArg = nullptr;

        // 提交 NPU 任务
        ret = ES_NPU_Submit(&task, 1);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_Submit failed");
            SAMPLE_NPU_COMM_DestroyTask(task);
            return ES_FAILURE;
        }

        // 后处理
        ret = SAMPLE_NPU_COMM_PostProcess(task, pictures.front(), outputDir, postProcess, false, taskTotalCount);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_PostProcess failed");
            SAMPLE_NPU_COMM_DestroyTask(task);
            return ES_FAILURE;
        }

        taskCount++;
        // 释放任务占用的资源
        SAMPLE_NPU_COMM_DestroyTask(task);
    }

    return ES_SUCCESS;
}

ES_S32 SAMPLE_NPU_COMMON_ConstructUserTask(ES_U32 modelId, npu_stream stream, std::vector<std::string> &inputFiles,
                                           std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                           NPU_TASK_S &task, std::vector<std::vector<std::string>> &pictures)
{
    ES_S32 ret;
    ES_S32 inputTensorId, outputTensorId;
    NPU_TASK_MEM_S taskMem;
    ES_U8 *pData = nullptr;

    ret = ES_NPU_AllocTaskMemory(modelId, 1, &taskMem);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_AllocTaskMemory failed\n");
        return ret;
    }

    if (taskMem.inputFdNum != inputFiles.size()) {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "inputFiles is not assigned\n");
        return ES_FAILURE;
    }

    for (inputTensorId = 0; inputTensorId < taskMem.inputFdNum; inputTensorId++) {
        // Read jepg image files
        std::vector<char> inFileData;
        ret = read_jpeg_data(inputFiles[inputTensorId].c_str(), inFileData);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "Failed to read_jpeg_data:%s\n", inputFiles[inputTensorId].c_str());
            return ES_FAILURE;
        }

        // Decode jpeg image to B8G8R8
        JPEG_DATA jpegData;
        jpegData.data = reinterpret_cast<uint8_t *>(inFileData.data());
        jpegData.len = static_cast<uint32_t>(inFileData.size());
        if (esVdec->decodeJpeg(jpegData) != ES_SUCCESS) {
            ret = ES_FAILURE;
            SAMPLE_NPU_DEBUG(ret, "Failed to decodeJpeg:%s\n", inputFiles[inputTensorId].c_str());
            return ES_FAILURE;
        }

        VDEC_VIDEO_FRAME frame;
        if (!esVdec->getVideoFrame(frame)) {
            ret = ES_FAILURE;
            SAMPLE_NPU_DEBUG(ret, "Failed to getVideoFrame:%s\n", inputFiles[inputTensorId].c_str());
            return ES_FAILURE;
        }

        // do preprocess
        PREPROCESSED_DATA processedData;
        processedData.fd = taskMem.inputFd[inputTensorId].memFd;
        processedData.offset = taskMem.inputFd[inputTensorId].offset;
        processedData.size = taskMem.inputFd[inputTensorId].size;
        ret = preProcess->preprocess(frame.videoInfoFrame, processedData);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "preprocess failed\n");
            esVdec->releaseFrame(frame);
            goto release_task;
        }

        // release video frame
        esVdec->releaseFrame(frame);

        if (pictures.size() <= inputTensorId) {
            pictures.push_back(std::vector<std::string>());
        }
        pictures[inputTensorId].push_back(inputFiles[inputTensorId]);

        task.inputFd[inputTensorId] = taskMem.inputFd[inputTensorId];
    }

    for (outputTensorId = 0; outputTensorId < taskMem.outputFdNum; outputTensorId++) {
        task.outputFd[outputTensorId] = taskMem.outputFd[outputTensorId];
    }

    task.inputFdNum = taskMem.inputFdNum;
    task.outputFdNum = taskMem.outputFdNum;
    task.modelId = modelId;

    return ES_SUCCESS;

release_task:
    ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);

    return ES_FAILURE;
}

/*
    +-----------+        +--------------+       +------------+        +-------------+
    | Read File |        |              |       |            |        |             |
    |   and     | -----> | Preprocess   | ----> | Inference  | -----> | Postprocess |
    | Decode    |        |              |       |            |        |             |
    +-----------+        +--------------+       +------------+        +-------------+

    Workflow:
    1. Read File: Open the file and load its content into memory.
    2. Decode: Decode the file content (e.g., image data).
    3. Preprocess: Prepare the decoded data for inference (e.g., resizing, normalization).
    4. Inference: Run the data through the model to get results.
    5. Postprocess: Process the inference results for output (e.g., display or save).
*/

ES_S32 SAMPLE_NPU_COMMON_SubmitPipelineTask(ES_U32 modelId, npu_stream stream,
                                            std::vector<std::vector<std::string>> inputFiles,
                                            const std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                                            std::shared_ptr<IPreprocess> preProcess,
                                            std::shared_ptr<EsPostProcess> postProcess)
{
    std::atomic<ES_S32> completeNum(0);
    std::atomic<bool> runTask = true;

    static int32_t npuTaskQueueMaxSize = 30;
    static std::mutex npuTaskMutex;
    static std::condition_variable npuTaskCond;
    static std::queue<NPU_TASK_S *> npuTaskQueue;
    static SampleStateInfo sampleInfo;

    // New thread to processReport
    sampleInfo.completeNum = &completeNum;
    SAMPLE_NPU_COMM_CreateQueryTrd(sampleInfo, stream);

    // Prepare progressbar
    uint32_t totalPictureCount = 0;
    for (const auto &dir : inputFiles) {
        totalPictureCount += dir.size();
    }
    showProgressBar(0, PROGRESS_BAR_WIDTH);

    // This thread is used for file reading and image decoding.
    std::thread decodeThread = std::thread([&modelId, esVdec, &inputFiles]() -> ES_S32 {
        ES_S32 ret;
        ES_S32 numInputs = 0;
        ES_S32 inputTensorId;
        pthread_setname_np(pthread_self(), "decodeThread");
        NPU_COMPOSITE_MODEL_INFO_S compositeModelInfo;
        ret = ES_NPU_GetCompositeModelInfo(modelId, &compositeModelInfo);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetCompositeModelInfo failed\n");
            return ret;
        }

        ret = ES_NPU_GetNumInputTensors(compositeModelInfo.modelsInfo[0].modelId, &numInputs);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_GetNumInputTensors failed");
            return ret;
        }

        // inputFiles: Represents a collection of directories and their respective subdirectories
        //
        // Structure:
        // inputFiles (std::vector<std::vector<std::string>>)
        // ├── Directory 1 (std::vector<std::string>)
        // │   ├── Subdirectory 1.1
        // │   ├── Subdirectory 1.2
        // │   └── Subdirectory 1.3
        // ├── Directory 2 (std::vector<std::string>)
        // │   ├── Subdirectory 2.1
        // │   └── Subdirectory 2.2
        // └── Directory 3 (std::vector<std::string>)
        //     └── Subdirectory 3.1
        //
        // The outer vector (inputFiles) holds multiple directories. Each directory is represented
        // by an inner vector (std::vector<std::string>) containing the names of subdirectories within that directory.
        static uint32_t count = 0;
        while (!inputFiles.front().empty()) {
            std::vector<std::string> inputPictures;

            for (auto &input : inputFiles) {
                inputPictures.push_back(input.front());
                input.erase(input.begin());
            }

            if (numInputs != inputPictures.size()) {
                SAMPLE_NPU_DEBUG(ES_FAILURE, "inputPictures is not assigned\n");
                return ES_FAILURE;
            }

            // The following logic describes that a model's input can consist of multiple tensors,
            // with each tensor corresponding to an image. After preprocessing,
            // these tensors can be fed into the model for inference.
            for (inputTensorId = 0; inputTensorId < numInputs; inputTensorId++) {
                // Read jepg image files
                std::vector<char> inFileData;
                ret = read_jpeg_data(inputPictures[inputTensorId].c_str(), inFileData);
                if (ret != ES_SUCCESS) {
                    SAMPLE_NPU_DEBUG(ret, "Failed to read_jpeg_data:%s\n", inputPictures[inputTensorId].c_str());
                    return ES_FAILURE;
                }

                // Decode jpeg image by VDEC
                JPEG_DATA jpegData;
                jpegData.data = reinterpret_cast<uint8_t *>(inFileData.data());
                jpegData.len = static_cast<uint32_t>(inFileData.size());
                jpegData.name = inputPictures[inputTensorId].c_str();

                if (esVdec->decodeJpeg(jpegData) != ES_SUCCESS) {
                    DEBUG_ERROR("Failed to decodeJpeg:%s\n", inputPictures[inputTensorId].c_str());
                    return ES_FAILURE;
                }
            }
        }

        return ES_SUCCESS;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // This is a callback used for post-processing the inference results.
    auto postProcessFunc = [](ES_VOID *args) -> ES_S32 {
        SampleCallbackArgs *sampleArgs = (SampleCallbackArgs *)args;
        static uint64_t count = 0;
        ES_S32 ret;

        // Dump output binary to file
        if (!sampleArgs->outputDir.empty()) {
            std::vector<std::string> pictures = sampleArgs->pictures.front();
            for (ES_S32 tensorId = 0; tensorId < sampleArgs->task->outputFdNum; tensorId++) {
                uint32_t fd = sampleArgs->task->outputFd[tensorId].memFd;
                uint32_t size = sampleArgs->task->outputFd[tensorId].size;
                uint32_t offset = sampleArgs->task->outputFd[tensorId].offset;
                uint8_t *pOutData = (uint8_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
                if (!pOutData) {
                    printf("ES_SYS_Mmap failed!\n");
                    return ES_FAILURE;
                }

                std::filesystem::path postprocessDir = std::filesystem::path(sampleArgs->outputDir) / "postprocess";
                if (!std::filesystem::exists(postprocessDir)) {
                    std::filesystem::create_directories(postprocessDir);
                }
                // Extract the filename and remove the file extension
                std::string fileName = std::filesystem::path(pictures.front()).filename().stem().string() + "_" +
                                       std::to_string(tensorId) + ".bin";

                std::ofstream outFile(postprocessDir / fileName, std::ios::binary);
                outFile.write(reinterpret_cast<const char *>(pOutData), size);
                if (!outFile) {
                    ret = ES_FAILURE;
                    SAMPLE_NPU_DEBUG(ret, "Failed to dump postprocess data to file: %s\n", fileName.c_str());
                    ES_SYS_Munmap(pOutData, size + offset);
                    return ret;
                }

                outFile.close();
                ES_SYS_Munmap(pOutData, size + offset);
            }
        }

        // Do post-process
        ret = SAMPLE_NPU_COMM_PostProcess(*sampleArgs->task, sampleArgs->pictures.front(), sampleArgs->outputDir,
                                          sampleArgs->postProcess, sampleArgs->userTask, sampleArgs->totalTask);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_PostProcess failed\n");
        }

        (*sampleArgs->completeNum)++;
        if (sampleArgs->userTask) {
            SAMPLE_NPU_COMM_DestroyUserTask(*sampleArgs->task, sampleArgs->stream);
        } else {
            SAMPLE_NPU_COMM_DestroyTask(*sampleArgs->task);
        }

        delete sampleArgs->task;
        delete sampleArgs;

        if ((sampleInfo.taskNum.load() - (*sampleInfo.completeNum).load()) <= 50) {
            std::unique_lock<std::mutex> lock(npuTaskMutex);
            npuTaskCond.notify_all();
        }

        count++;
        int32_t progress = count * PROGRESS_BAR_WIDTH / (sampleArgs->totalTask);
        showProgressBar(progress, PROGRESS_BAR_WIDTH);

        return ES_SUCCESS;
    };

    // This thread is used for pre-processing.
    std::thread preprocessThread =
        std::thread([modelId, &stream, esVdec, postProcess, &outputDir, preProcess, totalPictureCount, &runTask,
                     &completeNum, &postProcessFunc]() -> ES_S32 {
            pthread_setname_np(pthread_self(), "preprocessThread");
            while (true) {
                ES_S32 ret;
                ES_S32 inputTensorId, outputTensorId;
                NPU_TASK_S *task = nullptr;
                NPU_TASK_MEM_S taskMem;
                std::vector<std::vector<std::string>> pictures;

                // Allocate memory for the model's tasks.
                ret = ES_NPU_AllocTaskMemory(modelId, 1, &taskMem);
                if (ret != ES_SUCCESS) {
                    SAMPLE_NPU_DEBUG(ret, "ES_NPU_AllocTaskMemory failed\n");
                    break;
                }

                // The following logic represents performing preprocessing on each image corresponding to a tensor,
                // then filling the corresponding tensor memory, and constructing the inference task,
                // which is placed in the queue, waiting to be submitted.
                for (inputTensorId = 0; inputTensorId < taskMem.inputFdNum; inputTensorId++) {
                    // Get video frame video queue
                    VDEC_VIDEO_FRAME frame;
                    if (!esVdec->getVideoFrame(frame)) {
                        // printf("\nvideo frame is end\n");
                        ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);
                        return ES_SUCCESS;
                    }

                    // Do preprocess
                    PREPROCESSED_DATA processedData;
                    processedData.fd = taskMem.inputFd[inputTensorId].memFd;
                    processedData.offset = taskMem.inputFd[inputTensorId].offset;
                    processedData.size = taskMem.inputFd[inputTensorId].size;
                    ret = preProcess->preprocess(frame.videoInfoFrame, processedData);
                    if (ret != ES_SUCCESS) {
                        SAMPLE_NPU_DEBUG(ret, "preprocess failed\n");
                        ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);
                        esVdec->releaseFrame(frame);
                        return ret;
                    }

                    // Release video frame
                    esVdec->releaseFrame(frame);

                    // Allocate memory for the inference task
                    task = new (std::nothrow) NPU_TASK_S;
                    if (!task) {
                        ret = ES_FAILURE;
                        SAMPLE_NPU_DEBUG(ret, "new NPU_TASK_S failed, memory exception");
                        ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);
                        return ES_FAILURE;
                    }

                    memset(task->sdkPrivate, 0, ES_TASK_SDK_PRIVATE_LEN);
                    if (pictures.size() <= inputTensorId) {
                        pictures.push_back(std::vector<std::string>());
                    }
                    pictures[inputTensorId].push_back(frame.name);
                    task->inputFd[inputTensorId] = taskMem.inputFd[inputTensorId];

                    // Dump preprocess binary to file
                    if (!outputDir.empty()) {
                        uint32_t fd = taskMem.inputFd[inputTensorId].memFd;
                        uint32_t size = taskMem.inputFd[inputTensorId].size;
                        uint32_t offset = taskMem.inputFd[inputTensorId].offset;

                        uint8_t *pOutData = (uint8_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
                        if (!pOutData) {
                            printf("ES_SYS_Mmap failed!\n");
                            return ES_FAILURE;
                        }

                        std::filesystem::path preprocessDir = std::filesystem::path(outputDir) / "preprocess";
                        if (!std::filesystem::exists(preprocessDir)) {
                            std::filesystem::create_directories(preprocessDir);
                        }
                        // Extract the filename and remove the file extension
                        std::string fileName = std::filesystem::path(frame.name).filename().stem().string() + "_" +
                                               std::to_string(inputTensorId) + ".bin";

                        std::ofstream outFile(preprocessDir / fileName, std::ios::binary);
                        outFile.write(reinterpret_cast<const char *>(pOutData), size);
                        if (!outFile) {
                            ret = ES_FAILURE;
                            SAMPLE_NPU_DEBUG(ret, "Failed to dump preprocess data to file: %s\n", fileName.c_str());
                            ES_SYS_Munmap(pOutData, size + offset);
                            return ret;
                        }

                        outFile.close();
                        ES_SYS_Munmap(pOutData, size + offset);
                    }
                }

                for (outputTensorId = 0; outputTensorId < taskMem.outputFdNum; outputTensorId++) {
                    task->outputFd[outputTensorId] = taskMem.outputFd[outputTensorId];
                }

                SampleCallbackArgs *callbackArgs = new (std::nothrow) SampleCallbackArgs;
                if (!callbackArgs) {
                    ret = ES_FAILURE;
                    SAMPLE_NPU_DEBUG(ret, "new SampleCallbackArgs failed, memory exception");
                    ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);
                    delete task;
                    return ret;
                }

                // Construct task
                callbackArgs->pictures = pictures;
                callbackArgs->task = task;
                callbackArgs->postProcess = postProcess;
                callbackArgs->completeNum = &completeNum;
                callbackArgs->stream = stream;
                callbackArgs->userTask = true;
                callbackArgs->outputDir = outputDir;
                callbackArgs->totalTask = totalPictureCount;

                task->taskId = sampleInfo.taskNum++;
                task->callback = postProcessFunc;
                task->callbackArg = callbackArgs;

                task->inputFdNum = taskMem.inputFdNum;
                task->outputFdNum = taskMem.outputFdNum;
                task->modelId = modelId;

                // Push back task into queue, default maximum is 30 tasks.
                {
                    std::unique_lock<std::mutex> lock(npuTaskMutex);
                    npuTaskCond.wait(lock, [&runTask] {
                        return (npuTaskQueue.size() < (size_t)npuTaskQueueMaxSize) || !runTask.load();
                    });

                    if (!runTask.load()) {
                        DEBUG_INFO("%s: stop preprocess\n", __func__);
                        ES_NPU_ReleaseTaskMemory(modelId, 1, &taskMem);
                        return ES_FAILURE;
                    }

                    npuTaskQueue.emplace(task);
                    npuTaskCond.notify_all();
                }
            }

            return ES_SUCCESS;
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // This thread is the completion thread for all tasks, ensuring the proper exit of related threads.
    std::thread completionThread = std::thread([&decodeThread, &preprocessThread, esVdec, &runTask]() {
        pthread_setname_np(pthread_self(), "completionThread");
        // Wait for the decoding thread to exit.
        if (decodeThread.joinable()) {
            decodeThread.join();
        }
        // printf("completionThread: decode thread finished\n");

        // Wait for the preprocessing thread to exit.
        esVdec->flush();
        if (preprocessThread.joinable()) {
            preprocessThread.join();
        }
        // printf("completionThread: preprocess thread finished\n");

        // Notify the task submission thread that it can exit
        // after completing the tasks in the queue.
        std::unique_lock<std::mutex> lock(npuTaskMutex);
        runTask.store(false);
        npuTaskCond.notify_all();
        // printf("completionThread: run task thread finished\n");
    });

    // This is the main thread, responsible for submitting the inference tasks.
    ES_S32 ret;
    while (true) {
        NPU_TASK_S *task = nullptr;
        {
            std::unique_lock<std::mutex> lock(npuTaskMutex);
            npuTaskCond.wait(lock, [&runTask] {
                return (!npuTaskQueue.empty() || !runTask.load()) &&
                       ((sampleInfo.taskNum.load() - (*sampleInfo.completeNum).load()) < 150);
            });
            if (!runTask.load() && npuTaskQueue.empty()) {
                DEBUG_INFO("%s: stop inference\n", __func__);
                break;
            }

            task = npuTaskQueue.front();
            npuTaskQueue.pop();
            npuTaskCond.notify_all();
        }

        ret = ES_NPU_SubmitFlexibleTask(task, 1, stream);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_SubmitFlexibleTask failed");
            delete task;
            SAMPLE_NPU_COMM_DestroyUserTask(*task, stream);
            goto thread_exited;
        }
    }

    // loop for check all tasks complete.
    while ((*sampleInfo.completeNum).load() != sampleInfo.taskNum.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    printf("\nAll async tasks complete\n");
    if (completionThread.joinable()) completionThread.join();

thread_exited:
    sampleInfo.queryThreadExited.store(true);
    if (sampleInfo.queryThread.joinable()) {
        sampleInfo.queryThread.join();
    }

    return ret;
}

ES_S32 SAMPLE_NPU_COMMON_SubmitUserTask(ES_U32 modelId, npu_stream stream,
                                        std::vector<std::vector<std::string>> inputFiles, const std::string &outputDir,
                                        std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                        std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret;
    std::atomic<ES_S32> completeNum(0);
    SampleStateInfo sampleInfo;

    // New thread to processReport
    sampleInfo.completeNum = &completeNum;
    SAMPLE_NPU_COMM_CreateQueryTrd(sampleInfo, stream);
    uint64_t taskTotalCount = inputFiles.front().size();

    while (!inputFiles.front().empty()) {
        NPU_TASK_S *task = nullptr;
        SampleCallbackArgs *callbackArgs = nullptr;
        NPU_TASK_MEM_S taskMem;
        std::vector<std::string> inputPictures;
        std::vector<std::vector<std::string>> pictures;
        ES_U8 *pData = nullptr;

        task = new (std::nothrow) NPU_TASK_S;
        if (!task) {
            SAMPLE_NPU_DEBUG(ES_FAILURE, "new NPU_TASK_S failed, memory exception");
            ret = ES_FAILURE;
            goto thread_exited;
        }
        memset(task, 0, sizeof(NPU_TASK_S));
        callbackArgs = new (std::nothrow) SampleCallbackArgs;
        if (!callbackArgs) {
            SAMPLE_NPU_DEBUG(ES_FAILURE, "new SampleCallbackArgs failed, memory exception");
            ret = ES_FAILURE;
            delete task;
            goto thread_exited;
        }

        for (auto &input : inputFiles) {
            inputPictures.push_back(input.front());
            input.erase(input.begin());
        }

        ret = SAMPLE_NPU_COMMON_ConstructUserTask(modelId, stream, inputPictures, esVdec, preProcess, *task, pictures);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMMON_ConstructUserTask failed");
            delete task;
            delete callbackArgs;
            goto thread_exited;
        }
        callbackArgs->pictures = pictures;
        callbackArgs->task = task;
        callbackArgs->postProcess = postProcess;
        callbackArgs->completeNum = &completeNum;
        callbackArgs->stream = stream;
        callbackArgs->userTask = true;
        callbackArgs->outputDir = outputDir;
        callbackArgs->totalTask = taskTotalCount;
        task->taskId = sampleInfo.taskNum++;
        task->callback = SAMPLE_NPU_COMM_TaskCallback;
        task->callbackArg = callbackArgs;

        ret = ES_NPU_SubmitFlexibleTask(task, 1, stream);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_SubmitFlexibleTask failed");
            delete task;
            delete callbackArgs;
            SAMPLE_NPU_COMM_DestroyUserTask(*task, stream);
            goto thread_exited;
        }
    }

    // loop for check all tasks complete.
    while ((*sampleInfo.completeNum).load() != sampleInfo.taskNum.load()) {
        sleep(1);
    }

thread_exited:
    sampleInfo.queryThreadExited.store(true);
    if (sampleInfo.queryThread.joinable()) {
        sampleInfo.queryThread.join();
    }

    return ret;
}

ES_S32 SAMPLE_NPU_COMM_CreateUserTask(NPU_TASK_S **task, ES_U32 modelId, npu_stream stream,
                                      std::vector<std::vector<std::string>> &inputFiles, const std::string outputDir,
                                      std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                      std::shared_ptr<EsPostProcess> postProcess, std::mutex &mtx)
{
    ES_S32 ret;
    SampleCallbackArgs *callbackArgs = nullptr;
    std::vector<std::vector<std::string>> pictures;
    std::vector<std::string> inputPicture;
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (inputFiles.front().empty()) {
            *task = nullptr;
            return ES_SUCCESS;
        }

        for (auto &input : inputFiles) {
            inputPicture.push_back(input.front());
            input.erase(input.begin());
        }
    }
    *task = new (std::nothrow) NPU_TASK_S;
    if (!*task) {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "new NPU_TASK_S failed, memory exception");
        return ES_FAILURE;
    }

    memset((*task)->sdkPrivate, 0, ES_TASK_SDK_PRIVATE_LEN);
    callbackArgs = new (std::nothrow) SampleCallbackArgs;
    if (!callbackArgs) {
        SAMPLE_NPU_DEBUG(ES_FAILURE, "new SampleCallbackArgs failed, memory exception");
        delete *task;
        return ES_FAILURE;
    }

    ret = SAMPLE_NPU_COMMON_ConstructUserTask(modelId, stream, inputPicture, esVdec, preProcess, *(*task), pictures);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ConstructUserTask failed\n");
        delete *task;
        delete callbackArgs;
        return ret;
    }

    callbackArgs->pictures = pictures;
    callbackArgs->task = *task;
    callbackArgs->postProcess = postProcess;
    callbackArgs->stream = stream;
    callbackArgs->userTask = true;
    callbackArgs->outputDir = outputDir;
    callbackArgs->totalTask = inputPicture.size();
    (*task)->callback = SAMPLE_NPU_COMM_TaskCallback;
    (*task)->callbackArg = callbackArgs;

    return ES_SUCCESS;
}

ES_VOID *SAMPLE_NPU_COMM_D2DProcessTrd(ES_U16 devId, std::string modelPath,
                                       std::vector<std::vector<std::string>> &inputFiles, const std::string &outputDir,
                                       std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                       std::shared_ptr<EsPostProcess> postProcess, std::mutex &mtx)
{
    ES_S32 ret;
    ES_U32 modelId = 0;
    npu_context context;
    npu_stream stream;
    std::atomic<ES_S32> completeNum(0);
    SampleStateInfo sampleInfo;
    cpu_set_t cpuset;

    esBindThread2Die(devId);

    ret = SAMPLE_NPU_COMM_InitDevice(devId);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "SAMPLE_NPU_COMM_InitDevice: %d failed\n", devId);
        return NULL;
    }

    ret = ES_NPU_LoadCompositeModel(&modelId, modelPath.c_str());
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_LoadCompositeModel: %s failed\n", modelPath.c_str());
        goto load_model_failed;
    }

    ret = ES_NPU_CreateContext(&context, devId);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_CreateContext failed\n");
        goto create_context_failed;
    }

    ret = ES_NPU_CreateStream(&stream);
    if (ret != ES_SUCCESS) {
        SAMPLE_NPU_DEBUG(ret, "ES_NPU_CreateStream failed\n");
        goto create_stream_failed;
    }

    sampleInfo.completeNum = &completeNum;
    SAMPLE_NPU_COMM_CreateQueryTrd(sampleInfo, stream);
    while (true) {
        NPU_TASK_S *task = nullptr;
        ret = SAMPLE_NPU_COMM_CreateUserTask(&task, modelId, stream, inputFiles, outputDir, esVdec, preProcess,
                                             postProcess, mtx);
        if (ret != ES_SUCCESS || task == nullptr) {
            break;
        }

        task->taskId = sampleInfo.taskNum++;
        ((SampleCallbackArgs *)(task->callbackArg))->completeNum = &completeNum;

        ret = ES_NPU_SubmitFlexibleTask(task, 1, stream);
        if (ret != ES_SUCCESS) {
            SAMPLE_NPU_DEBUG(ret, "ES_NPU_SubmitFlexibleTask failed");
            delete (SampleCallbackArgs *)(task->callbackArg);
            delete task;
            SAMPLE_NPU_COMM_DestroyUserTask(*task, stream);
            goto exited;
        }
    }

    // loop for check all tasks complete.
    while ((*sampleInfo.completeNum).load() != sampleInfo.taskNum.load()) {
        sleep(1);
    }

exited:
    sampleInfo.queryThreadExited.store(true);
    if (sampleInfo.queryThread.joinable()) {
        sampleInfo.queryThread.join();
    }

    ES_NPU_DestroyStream(stream);

create_stream_failed:
    ES_NPU_DestroyContext(context);

create_context_failed:
    ES_NPU_UnloadCompositeModel(modelId);

load_model_failed:
    SAMPLE_NPU_COMM_ReleaseDevice(devId);

    return NULL;
}

void esBindThread2Die(int32_t dieId)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (dieId == 0) {
        CPU_SET(0, &cpuset);
        CPU_SET(1, &cpuset);
        CPU_SET(2, &cpuset);
        CPU_SET(3, &cpuset);
    } else {
        CPU_SET(4, &cpuset);
        CPU_SET(5, &cpuset);
        CPU_SET(6, &cpuset);
        CPU_SET(7, &cpuset);
    }

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}