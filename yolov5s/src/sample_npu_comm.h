// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef SAMPLE_NPU_COMMON_H
#define SAMPLE_NPU_COMMON_H

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include "es_npu_interface.h"
#include "es_sys_memory.h"
#include "IVdec.h"
#include "IPreprocess.h"
#include "EsPostProcess.h"

typedef struct {
    std::thread queryThread;
    std::atomic<bool> queryThreadExited;
    std::atomic<ES_S32> taskNum;
    std::atomic<ES_S32> *completeNum;
    npu_stream stream;
    ES_S32 queryMs;
} SampleStateInfo;

typedef struct {
    std::vector<std::vector<std::string>> pictures;
    std::string outputDir;
    NPU_TASK_S *task;
    std::shared_ptr<EsPostProcess> postProcess;
    std::atomic<ES_S32> *completeNum;
    npu_stream stream;
    uint64_t totalTask;
    bool userTask;
} SampleCallbackArgs;

ES_S32 SAMPLE_NPU_COMM_InitDevice(ES_U16 devId);
ES_S32 SAMPLE_NPU_COMM_ReleaseDevice(ES_U16 devId);
ES_S32 SAMPLE_NPU_COMM_ConstructTask(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> &imgPath,
                                     std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> process,
                                     NPU_TASK_S &task, std::vector<std::vector<std::string>> &pictures);
ES_VOID SAMPLE_NPU_COMM_DestroyTask(NPU_TASK_S &task);
ES_S32 SAMPLE_NPU_COMMON_ConstructUserTask(ES_U32 modelId, npu_stream stream, std::vector<std::string> &inputFiles,
                                           std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                           NPU_TASK_S &task, std::vector<std::vector<std::string>> &pictures);
ES_VOID SAMPLE_NPU_COMM_DestroyUserTask(NPU_TASK_S &task, npu_stream stream);
ES_S32 SAMPLE_NPU_COMM_PostProcess(NPU_TASK_S &task, std::vector<std::string> &pictures, const std::string &outputDir,
                                   std::shared_ptr<EsPostProcess> postProcess, bool isFlexibleTask,
                                   uint64_t taskTotalCount);
ES_VOID *SAMPLE_NPU_COMM_QueryThread(ES_VOID *args);
ES_S32 SAMPLE_NPU_COMM_CreateQueryTrd(SampleStateInfo &info, npu_stream stream);
ES_S32 SAMPLE_NPU_COMM_TaskCallback(ES_VOID *args);
ES_S32 SAMPLE_NPU_COMM_UserTaskCallback(ES_VOID *args);
ES_S32 SAMPLE_NPU_COMM_SubmitAsync(ES_U16 devId, ES_U32 modelId, npu_stream stream,
                                   std::vector<std::vector<std::string>> inputFiles, const std::string &outputDir,
                                   std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                   std::shared_ptr<EsPostProcess> postProcess);
ES_S32 SAMPLE_NPU_COMM_SubmitSync(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> inputFiles,
                                  const std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                                  std::shared_ptr<IPreprocess> preProcess, std::shared_ptr<EsPostProcess> postProcess);
ES_S32 SAMPLE_NPU_COMMON_SubmitUserTask(ES_U32 modelId, npu_stream stream,
                                        std::vector<std::vector<std::string>> inputFiles, const std::string &outputDir,
                                        std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                        std::shared_ptr<EsPostProcess> postProcess);
ES_S32 SAMPLE_NPU_COMMON_SubmitPipelineTask(ES_U32 modelId, npu_stream stream,
                                            std::vector<std::vector<std::string>> inputFiles,
                                            const std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                                            std::shared_ptr<IPreprocess> preProcess,
                                            std::shared_ptr<EsPostProcess> postProcess);
ES_VOID *SAMPLE_NPU_COMM_D2DProcessTrd(ES_U16 devId, std::string modelPath,
                                       std::vector<std::vector<std::string>> &inputFiles, const std::string &outputDir,
                                       std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                                       std::shared_ptr<EsPostProcess> postProcess, std::mutex &mtx);
void esBindThread2Die(int32_t dieId);

#endif  // SAMPLE_NPU_COMMON_H