// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "EsPostProcess.h"
#include "es_ak_api.h"
#include "es_sys_memory.h"
#include "sample_npu_utils.h"
#include "cJSON.h"

#define DEBUG_ON 0
#define INVALID_POST_PROCESS_VALUE -1
static const uint32_t DSP_CORE_NUM = 4;

static const std::unordered_map<std::string, int32_t> PROCESS_MAP = {
    {"detection_out", EsPostProcess::DETECTION_OUT},
    {"softmax", EsPostProcess::CLASSIFY},
    {"rtmpose", EsPostProcess::RTMPOSE},
};

static const std::unordered_map<std::string, int32_t> NETWORK_MAP = {
    {"yolov3_u", DetectOutConfigs::ES_YOLOV3_U},  {"yolov3_d", DetectOutConfigs::ES_YOLOV3_D},
    {"yolov5", DetectOutConfigs::ES_YOLOV5},      {"yolov4", DetectOutConfigs::ES_YOLOV4},
    {"yolov7", DetectOutConfigs::ES_YOLOV7},      {"yolov8", DetectOutConfigs::ES_YOLOV8},
    {"damo_yolo", DetectOutConfigs::ES_DAMOYOLO},
};

static const std::unordered_map<std::string, int32_t> DATATYPE_MAP = {
    {"S8", ES_PRECISION_INT8},  {"S16", ES_PRECISION_INT16}, {"I32", ES_PRECISION_INT32},
    {"F16", ES_PRECISION_FP16}, {"F32", ES_PRECISION_FP32},
};

static const std::unordered_map<std::string, int32_t> NMS_METHOD_MAP = {
    {"hard_nms", DetectOutConfigs::ES_HARD_NMS},
    {"soft_nms", DetectOutConfigs::ES_SOFT_NMS_GAUSSIAN},
    {"soft_nms_linear", DetectOutConfigs::ES_SOFT_NMS_LINEAR},
};

static const std::unordered_map<std::string, int32_t> IOU_METHOD_MAP = {
    {"iou", DetectOutConfigs::ES_IOU},
    {"giou", DetectOutConfigs::ES_GIOU},
    {"diou", DetectOutConfigs::ES_GIOU},
};

static const std::unordered_map<std::string, int32_t> BOX_TYPE_MAP = {
    {"xminyminxmaxymax", DetectOutConfigs::ES_XMIN_YMIN_XMAX_YMAX},
    {"xminyminwh", DetectOutConfigs::ES_XMIN_YMIN_W_H},
    {"yminxminymaxxmax", DetectOutConfigs::ES_YMIN_XMIN_YMAX_XMAX},
    {"xmindymindwh", DetectOutConfigs::ES_XMID_YMID_W_H},
};

int32_t EsPostProcess::mInstanceCount = 0;

EsPostProcess::EsPostProcess(uint16_t dieId)
    : mProcessType(CLASSIFY),
      mArgmaxK(1),
      mStartIndex(INVALID_POST_PROCESS_VALUE),
      mNumElements(INVALID_POST_PROCESS_VALUE),
      mDieId(dieId)
{
    if (mInstanceCount == 0) {
        if (ES_AK_Init() != 0) {
            printf("ES_AK_Init failed\n");
        }
    }
    ES_AK_DEVICE_E device = (ES_AK_DEVICE_E)(mDieId * DSP_CORE_NUM);
    ES_AK_SetDevice(&device, 1);
    mInstanceCount++;
    mParams.keepRatio = false;
    mParams.keepLongSide = false;
    mParams.paddingPosition.clear();
    mProcessCount = 0;
    // ES_AK_SetLogLevel(ES_AK_LOG_DEBUG);
}

EsPostProcess::~EsPostProcess()
{
    mInstanceCount--;
    if (mInstanceCount == 0) {
        ES_AK_Deinit();
    }
}

bool EsPostProcess::parseArgs(std::string fileName)
{
    std::ifstream file(fileName);
    std::string str;

    if (!file.is_open()) {
        printf("Failed to open JSON file\n");
        return false;
    }

    std::string jsonString((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    cJSON *root = cJSON_Parse(jsonString.c_str());
    if (!root) {
        printf("Failed to parse JSON\n");
        return false;
    }

    mProcessName = cJSON_GetObjectItem(root, "kernel_name")->valuestring;
    mProcessType = (POST_PROCESS_TYPE)PROCESS_MAP.at(mProcessName);

    if (mProcessType == DETECTION_OUT) {
        str = cJSON_GetObjectItem(root, "detection_net")->valuestring;
        mDetectOutConfigs.detectNet = (DetectOutConfigs::EsDetNetwork)NETWORK_MAP.at(str);
        mDetectOutConfigs.inTensorNum = cJSON_GetObjectItem(root, "in_tensor_num")->valueint;
        mDetectOutConfigs.outTensorNum = cJSON_GetObjectItem(root, "out_tensor_num")->valueint;

        str = cJSON_GetObjectItem(root, "input_shape")->valuestring;
        mDetectOutConfigs.inputShape = parseDataShape(str);
        str = cJSON_GetObjectItem(root, "output_shape")->valuestring;
        mDetectOutConfigs.outputShape = parseDataShape(str);

        str = cJSON_GetObjectItem(root, "input_data_type")->valuestring;
        mDetectOutConfigs.inputDataType = parseDataType(str);
        str = cJSON_GetObjectItem(root, "output_data_type")->valuestring;
        mDetectOutConfigs.outputDataType = parseDataType(str);

        if (cJSON_GetObjectItem(root, "anchors_num") != NULL) {
            mDetectOutConfigs.anchorsNum = cJSON_GetObjectItem(root, "anchors_num")->valueint;
        } else {
            mDetectOutConfigs.anchorsNum = 0;
        }

        if (cJSON_GetObjectItem(root, "anchor_scale_table") != NULL) {
            str = cJSON_GetObjectItem(root, "anchor_scale_table")->valuestring;
            mDetectOutConfigs.anchorScale = parseArray<float>(str);
        }

        mDetectOutConfigs.imgH = cJSON_GetObjectItem(root, "img_h")->valueint;
        mDetectOutConfigs.imgW = cJSON_GetObjectItem(root, "img_w")->valueint;
        mDetectOutConfigs.clsNum = cJSON_GetObjectItem(root, "cls_num")->valueint;

        str = cJSON_GetObjectItem(root, "input_scale")->valuestring;
        mDetectOutConfigs.inputScale = parseArray<float>(str);
        str = cJSON_GetObjectItem(root, "nms_method")->valuestring;
        mDetectOutConfigs.nmsMethod = (DetectOutConfigs::EsNmsMethod)NMS_METHOD_MAP.at(str);
        str = cJSON_GetObjectItem(root, "iou_method")->valuestring;
        mDetectOutConfigs.iouMethod = (DetectOutConfigs::EsIouMethod)IOU_METHOD_MAP.at(str);
        str = cJSON_GetObjectItem(root, "out_box_type")->valuestring;
        mDetectOutConfigs.outBoxType = (DetectOutConfigs::EsBoxType)BOX_TYPE_MAP.at(str);

        mDetectOutConfigs.coordNorm = cJSON_GetObjectItem(root, "coord_norm_flag")->valueint;

        mDetectOutConfigs.maxBoxesPerClass = cJSON_GetObjectItem(root, "max_boxes_per_class")->valueint;
        mDetectOutConfigs.maxBoxesPerBatch = cJSON_GetObjectItem(root, "max_boxes_per_batch")->valueint;
        mDetectOutConfigs.scoreThreshold = cJSON_GetObjectItem(root, "score_threshold")->valuedouble;
        mDetectOutConfigs.iouThreshold = cJSON_GetObjectItem(root, "iou_threshold")->valuedouble;
        mDetectOutConfigs.softNmsSigma = cJSON_GetObjectItem(root, "soft_nms_sigma")->valuedouble;
        mDetectOutConfigs.effecImgOffsetX = cJSON_GetObjectItem(root, "effec_img_offset_x")->valuedouble;
        mDetectOutConfigs.effecImgOffsetY = cJSON_GetObjectItem(root, "effec_img_offset_y")->valuedouble;
    } else if (mProcessType == CLASSIFY) {
        // mArgmaxK = 5;
        mArgmaxK = cJSON_GetObjectItem(root, "topk")->valueint;
        mScale = cJSON_GetObjectItem(root, "scale")->valuedouble;

        if (cJSON_GetObjectItem(root, "start_index") != NULL) {
            mStartIndex = cJSON_GetObjectItem(root, "start_index")->valueint;
        }

        if (cJSON_GetObjectItem(root, "num_elements") != NULL) {
            mNumElements = cJSON_GetObjectItem(root, "num_elements")->valueint;
        }
    } else if (mProcessType == RTMPOSE) {
        mRtmPoseConfigs.imgH = cJSON_GetObjectItem(root, "img_h")->valueint;
        mRtmPoseConfigs.imgW = cJSON_GetObjectItem(root, "img_w")->valueint;
        mRtmPoseConfigs.scaleX = cJSON_GetObjectItem(root, "scale_x")->valuedouble;
        mRtmPoseConfigs.scaleY = cJSON_GetObjectItem(root, "scale_y")->valuedouble;
    } else {
        // TODO: segment
    }

    return true;
}

int32_t EsPostProcess::getProcessType() { return mProcessType; }

int32_t EsPostProcess::getArgmaxK() { return mArgmaxK; }

int32_t EsPostProcess::rtmPose(std::vector<ES_TENSOR_S> inTensors, std::vector<ES_POSE_POINT> &poseResult)
{
    int32_t ret;
    int32_t extend_width = inTensors[0].shape[2];
    int32_t extend_height = inTensors[1].shape[2];
    int32_t numPoints = inTensors[0].shape[1];

    std::vector<int16_t> simccXResult;
    std::vector<int16_t> simccYResult;

    float scaleX = mRtmPoseConfigs.scaleX;
    float scaleY = mRtmPoseConfigs.scaleY;

    if (inTensors.size() != 2) {
        printf("select_max input tensor size is not 2\n");
        return -1;
    }

    uint32_t fd = inTensors[0].pData.memFd;
    uint32_t size = inTensors[0].pData.size;
    uint32_t offset = inTensors[0].pData.offset;
    int16_t *pOutData = (int16_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
    if (!pOutData) {
        printf("Mmap tensor[0] failed!\n");
        return ES_FAILURE;
    }

    simccXResult.assign(pOutData + offset, pOutData + offset + size / sizeof(int16_t));
    ES_SYS_Munmap(pOutData, size + offset);

    fd = inTensors[1].pData.memFd;
    size = inTensors[1].pData.size;
    offset = inTensors[1].pData.offset;
    pOutData = (int16_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
    if (!pOutData) {
        printf("Mmap tensor[1] failed!\n");
        return ES_FAILURE;
    }

    simccYResult.assign(pOutData + offset, pOutData + offset + size / sizeof(int16_t));
    ES_SYS_Munmap(pOutData, size + offset);

    std::vector<float> floatVecX(simccXResult.size());
    std::vector<float> floatVecY(simccYResult.size());

    std::transform(simccXResult.begin(), simccXResult.end(), floatVecX.begin(),
                   [scaleX](int16_t x) { return static_cast<float>(x) * scaleX; });

    std::transform(simccYResult.begin(), simccYResult.end(), floatVecY.begin(),
                   [scaleY](int16_t x) { return static_cast<float>(x) * scaleY; });

    for (uint32_t i = 0; i < numPoints; ++i) {
        // find the maximum and maximum indexes in the value of each Extend_width length
        auto xBiggestIter =
            std::max_element(floatVecX.begin() + i * extend_width, floatVecX.begin() + i * extend_width + extend_width);
        uint32_t poseX = (std::distance(floatVecX.begin() + i * extend_width, xBiggestIter)) / 2;
        float scoreX = *xBiggestIter;

        // find the maximum and maximum indexes in the value of each exten_height length
        auto yBiggestIter = std::max_element(floatVecY.begin() + i * extend_height,
                                             floatVecY.begin() + i * extend_height + extend_height);
        uint32_t poseY = (std::distance(floatVecY.begin() + i * extend_height, yBiggestIter)) / 2;
        float scoreY = *yBiggestIter;

        // get point confidence
        float score = std::max(scoreX, scoreY);
        ES_POSE_POINT tempPoint;
        tempPoint.x = poseX;
        tempPoint.y = poseY;
        tempPoint.score = score;
        poseResult.emplace_back(tempPoint);
    }

    return 0;
}

int32_t EsPostProcess::classify(std::vector<ES_TENSOR_S> inTensors, std::vector<std::vector<int>> &topKIndex,
                                std::vector<std::vector<float>> &topKConfidence)
{
    int32_t ret;
    int32_t argmaxK = mArgmaxK;
    ES_TENSOR_S softmaxOutput, argmaxOutput, argmaxOutputIdx;
    void *pArgmaxOutput = NULL, *pArgmaxOutputIdx = NULL;
    int softmaxOutputSize, argmaxOutputSize, argmaxOutputIdxSize;

    if (inTensors.size() != 1) {
        printf("classify input tensor size is not 1\n");
        return -1;
    }

    memset(&softmaxOutput, 0x00, sizeof(ES_TENSOR_S));
    memset(&argmaxOutput, 0x00, sizeof(ES_TENSOR_S));
    memset(&argmaxOutputIdx, 0x00, sizeof(ES_TENSOR_S));

    /* For certain models, such as InceptionV4 (1,1001,1,1),
     * only the last 1000 values are used in the post processing.
     * Therefore, special handling of the start index and the number of elements is required.
     */
    if (mNumElements != INVALID_POST_PROCESS_VALUE) {
        inTensors[0].shape[1] = mNumElements;
        inTensors[0].shape[5] = inTensors[0].shape[4] * inTensors[0].shape[1];
    }
    if (mStartIndex != INVALID_POST_PROCESS_VALUE) {
        inTensors[0].pData.offset = mStartIndex;
        inTensors[0].pData.size = mNumElements * convertPrecisionToBytes(inTensors[0].dataType);
    }

    shapeCopy(softmaxOutput, inTensors[0]);
    softmaxOutput.dataType = ES_PRECISION_FP32;
    softmaxOutputSize = softmaxOutput.shape[0] * softmaxOutput.shape[1] * softmaxOutput.shape[2] *
                        softmaxOutput.shape[3] * softmaxOutput.shape[4] *
                        convertPrecisionToBytes(softmaxOutput.dataType);
    if (prepareSysMem(softmaxOutput.pData.memFd, softmaxOutputSize) != 0) {
        printf("prepareSysMem failed\n");
        return -1;
    }
    softmaxOutput.pData.offset = 0;
    softmaxOutput.pData.size = softmaxOutputSize;
    ret = ES_AK_DSP_Softmax((ES_AK_DEVICE_E)(mDieId * DSP_CORE_NUM), inTensors[0], softmaxOutput, mScale);
    if (ret != 0) {
        printf("ES_AK_DSP_Softmax failed\n");
        goto exited;
    }

    shapeCopy(argmaxOutput, softmaxOutput);
    argmaxOutput.shape[1] = argmaxK;
    argmaxOutput.dataType = ES_PRECISION_FP32;
    argmaxOutputSize = argmaxOutput.shape[0] * argmaxOutput.shape[1] * argmaxOutput.shape[2] * argmaxOutput.shape[3] *
                       argmaxOutput.shape[4] * convertPrecisionToBytes(argmaxOutput.dataType);
    ret = prepareSysMem(argmaxOutput.pData.memFd, argmaxOutputSize);
    if (ret != 0) {
        printf("prepareSysMem failed\n");
        goto exited;
    }
    argmaxOutput.pData.offset = 0;
    argmaxOutput.pData.size = argmaxOutputSize;

    shapeCopy(argmaxOutputIdx, argmaxOutput);
    argmaxOutputIdx.dataType = ES_PRECISION_UINT16;
    argmaxOutputIdxSize = argmaxOutputIdx.shape[0] * argmaxOutputIdx.shape[1] * argmaxOutputIdx.shape[2] *
                          argmaxOutputIdx.shape[3] * argmaxOutputIdx.shape[4] *
                          convertPrecisionToBytes(argmaxOutputIdx.dataType);
    ret = prepareSysMem(argmaxOutputIdx.pData.memFd, argmaxOutputIdxSize);
    if (ret != 0) {
        printf("prepareSysMem failed\n");
        goto exited;
    }
    argmaxOutputIdx.pData.offset = 0;
    argmaxOutputIdx.pData.size = argmaxOutputIdxSize;
    ret = ES_AK_DSP_Argmax((ES_AK_DEVICE_E)(mDieId * DSP_CORE_NUM), softmaxOutput, argmaxOutput, argmaxOutputIdx, argmaxK, 1);
    if (ret != 0) {
        printf("ES_AK_DSP_Argmax failed\n");
        goto exited;
    }

    pArgmaxOutput = ES_SYS_Mmap(argmaxOutput.pData.memFd, argmaxOutput.pData.size, SYS_CACHE_MODE_NOCACHE);
    if (pArgmaxOutput == NULL) {
        printf("ES_SYS_Mmap failed\n");
        ret = -1;
        goto exited;
    }
    for (int i = 0; i < argmaxOutput.shape[0]; i++) {
        std::vector<float> confidence;
        for (int j = 0; j < argmaxOutput.shape[1]; j++) {
            confidence.push_back(((float *)pArgmaxOutput)[i * argmaxOutput.shape[1] + j]);
        }
        topKConfidence.push_back(confidence);
    }

    pArgmaxOutputIdx = ES_SYS_Mmap(argmaxOutputIdx.pData.memFd, argmaxOutputIdx.pData.size, SYS_CACHE_MODE_NOCACHE);
    if (pArgmaxOutputIdx == NULL) {
        printf("ES_SYS_Mmap failed\n");
        ret = -1;
        goto exited;
    }
    for (int i = 0; i < argmaxOutputIdx.shape[0]; i++) {
        std::vector<int> index;
        for (int j = 0; j < argmaxOutputIdx.shape[1]; j++) {
            index.push_back(((uint16_t *)pArgmaxOutputIdx)[i * argmaxOutput.shape[1] + j]);
        }
        topKIndex.push_back(index);
    }

exited:
    if (pArgmaxOutput != NULL) {
        ES_SYS_Munmap(pArgmaxOutput, argmaxOutput.pData.size);
    }
    if (pArgmaxOutputIdx != NULL) {
        ES_SYS_Munmap(pArgmaxOutputIdx, argmaxOutputIdx.pData.size);
    }
    if (softmaxOutput.pData.memFd != 0) {
        ES_SYS_MemFree(softmaxOutput.pData.memFd);
    }
    if (argmaxOutput.pData.memFd != 0) {
        ES_SYS_MemFree(argmaxOutput.pData.memFd);
    }
    if (argmaxOutputIdx.pData.memFd != 0) {
        ES_SYS_MemFree(argmaxOutputIdx.pData.memFd);
    }

    return ret;
}

int32_t EsPostProcess::detectionOut(std::vector<ES_TENSOR_S> inTensors,
                                    std::vector<std::vector<std::vector<float>>> &boxInfo)
{
    std::lock_guard<std::mutex> lock(mMtx);

    ES_TENSOR_S output, outputCount;
    int32_t outputSize, outputCountSize;
    void *outputData = nullptr;
    void *outputCountData = nullptr;
    std::vector<int32_t> boxCount;
    ES_DETECTION_OUT_CFG detectCfg;
    int32_t ret;
#if DEBUG_ON
    std::ofstream outFileStream0("output_dsp0.bin", std::ios::out | std::ios::binary);
    std::ofstream outFileStream1("output_dsp1.bin", std::ios::out | std::ios::binary);
#endif

    memset(&output, 0x00, sizeof(output));
    memset(&outputCount, 0x00, sizeof(outputCount));
    output.shapeDim = 5;
    output.shape[0] = 1;
    output.shape[1] = 7;  // b_id, clsid, score, x, y, x, y
    output.shape[2] = 1;
    output.shape[3] = mDetectOutConfigs.maxBoxesPerBatch;
    output.shape[4] = 1;
    output.dataType = (ES_DATA_PRECISION_E)mDetectOutConfigs.outputDataType[0];
    outputSize = output.shape[0] * output.shape[1] * output.shape[2] * output.shape[3] * output.shape[4] *
                 convertPrecisionToBytes(output.dataType);
    output.pData.offset = 0;
    output.pData.size = outputSize;

    ret = prepareSysMem(output.pData.memFd, outputSize);
    if (ret != ES_AK_SUCCESS) {
        printf("prepareSysMem output failed\n");
        return ret;
    }

    outputCount.shapeDim = 5;
    outputCount.shape[0] = mDetectOutConfigs.inputShape[0][0];
    outputCount.shape[1] = 1;
    outputCount.shape[2] = 1;
    outputCount.shape[3] = 1;
    outputCount.shape[4] = 1;
    outputCount.dataType = (ES_DATA_PRECISION_E)mDetectOutConfigs.outputDataType[1];
    outputCountSize = outputCount.shape[0] * outputCount.shape[1] * outputCount.shape[2] * outputCount.shape[3] *
                      outputCount.shape[4] * convertPrecisionToBytes(outputCount.dataType);
    outputCount.pData.offset = 0;
    outputCount.pData.size = outputCountSize;

    ret = prepareSysMem(outputCount.pData.memFd, outputCountSize);
    if (ret != ES_AK_SUCCESS) {
        printf("prepareSysMem outputCount failed, ret: 0x%x!\n", ret);
        goto exited;
    }

    memset(&detectCfg, 0x00, sizeof(detectCfg));
    detectCfg.anchorsNum = mDetectOutConfigs.anchorsNum;
    std::copy(mDetectOutConfigs.anchorScale.begin(), mDetectOutConfigs.anchorScale.end(), detectCfg.anchorScale);
    detectCfg.imgH = mDetectOutConfigs.imgH;
    detectCfg.imgW = mDetectOutConfigs.imgW;
    detectCfg.clsNum = mDetectOutConfigs.clsNum;
    std::copy(mDetectOutConfigs.inputScale.begin(), mDetectOutConfigs.inputScale.end(), detectCfg.inputScale);
    detectCfg.nmsMethod = (ES_NMS_METHOD_E)mDetectOutConfigs.nmsMethod;
    detectCfg.iouMethod = (ES_IOU_METHOD_E)mDetectOutConfigs.iouMethod;
    detectCfg.outBoxType = (ES_BOX_TYPE_E)mDetectOutConfigs.outBoxType;
    detectCfg.coordNorm = (ES_BOOL)mDetectOutConfigs.coordNorm;
    detectCfg.maxBoxesPerClass = mDetectOutConfigs.maxBoxesPerClass;
    detectCfg.maxBoxesPerBatch = mDetectOutConfigs.maxBoxesPerBatch;
    detectCfg.scoreThreshold = mDetectOutConfigs.scoreThreshold;
    detectCfg.iouThreshold = mDetectOutConfigs.iouThreshold;
    detectCfg.softNmsSigma = mDetectOutConfigs.softNmsSigma;
    detectCfg.effecImgOffsetX = mDetectOutConfigs.effecImgOffsetX;
    detectCfg.effecImgOffsetY = mDetectOutConfigs.effecImgOffsetY;

    ret = ES_AK_DSP_DetectionOut((ES_AK_DEVICE_E)(mDieId * DSP_CORE_NUM), inTensors.data(), inTensors.size(), output,
                                 outputCount, (ES_DET_NETWORK_E)mDetectOutConfigs.detectNet, &detectCfg);
    if (ret != ES_AK_SUCCESS) {
        printf("ES_AK_DSP_DetectionOut failed.\n");
        goto exited;
    }

    outputData = ES_SYS_Mmap(output.pData.memFd, output.pData.size, SYS_CACHE_MODE_NOCACHE);
    if (!outputData) {
        printf("output ES_SYS_Mmap failed\n");
        goto exited;
    }

    outputCountData = ES_SYS_Mmap(outputCount.pData.memFd, outputCount.pData.size, SYS_CACHE_MODE_NOCACHE);
    if (!outputCountData) {
        printf("outputCount ES_SYS_Mmap failed\n");
        goto exited;
    }

#if DEBUG_ON
    outFileStream0.write((char *)outputData, output.pData.size);
    outFileStream0.close();
    outFileStream1.write((char *)outputCountData, outputCount.pData.size);
    outFileStream1.close();
#endif

    if (outputCount.dataType == ES_PRECISION_INT32) {
        boxCount = extractBoxNum<int32_t>(outputCountData, (int32_t)outputCount.shape[0]);
    } else {
        printf("post process box count not support datatype: %d\n", outputCount.dataType);
    }

    if (output.dataType == ES_PRECISION_FP32) {
        boxInfo = extractBoxInfo<float>(outputData, boxCount, (int32_t)output.shape[1]);
    } else {
        printf("post process box info not supported data type: %d\n", output.dataType);
    }

exited:
    if (outputData) {
        ES_SYS_Munmap(outputData, output.pData.size);
    }
    if (outputCountData) {
        ES_SYS_Munmap(outputCountData, outputCount.pData.size);
    }
    if (output.pData.memFd != 0) {
        ES_SYS_MemFree(output.pData.memFd);
    }
    if (outputCount.pData.memFd != 0) {
        ES_SYS_MemFree(outputCount.pData.memFd);
    }

    return ret;
}

int32_t EsPostProcess::segment(std::vector<ES_TENSOR_S> inTensors, void *out) { return 0; }

bool EsPostProcess::parseClassification(std::string fileName)
{
    std::ifstream file(fileName);
    std::vector<std::string> splitResult;
    int key;

    if (file.is_open()) {
        std::string line;
        while (getline(file, line)) {
            // Parse label info
            splitResult = split(line, ':');
            if (splitResult.size() == 2) {
                key = std::stoi(splitResult[0]);
                splitResult = split(splitResult[1], ',');
                mLabelMap[key] = splitResult;
            }
        }
    } else {
        printf("Open class file failed, file %s\n", fileName.c_str());
        return false;
    }

    return true;
}

std::string EsPostProcess::getLabelByIdx(int idx, bool full)
{
    if (mLabelMap.find(idx) == mLabelMap.end()) {
        printf("Invalid idx: %d\n", idx);
        return "";
    }

    std::string result;
    std::vector<std::string> value = mLabelMap[idx];

    result = value[0];
    if (full) {
        for (int i = 1; i < value.size(); i++) {
            result += ", " + value[i];
        }
    }

    return result;
}

void EsPostProcess::shapeCopy(ES_TENSOR_S &dst, ES_TENSOR_S &src)
{
    dst.shapeDim = src.shapeDim;
    dst.shape[0] = src.shape[0];
    dst.shape[1] = src.shape[1];
    dst.shape[2] = src.shape[2];
    dst.shape[3] = src.shape[3];
    dst.shape[4] = src.shape[4];
    dst.shape[5] = src.shape[5];
}

ES_DATA_PRECISION_E EsPostProcess::convertDataType(uint8_t dataType)
{
    switch (dataType) {
        case ES_NPU_DATA_TYPE_FLOAT:
            return ES_PRECISION_FP32;
        case ES_NPU_DATA_TYPE_HALF:
            return ES_PRECISION_FP16;
        case ES_NPU_DATA_TYPE_INT16:
            return ES_PRECISION_INT16;
        case ES_NPU_DATA_TYPE_INT8:
            return ES_PRECISION_INT8;
        case ES_NPU_DATA_TYPE_UINT8:
            return ES_PRECISION_UINT8;
        case ES_NPU_DATA_TYPE_UINT16:
            return ES_PRECISION_UINT16;
        case ES_NPU_DATA_TYPE_INT32:
            return ES_PRECISION_INT32;
        case ES_NPU_DATA_TYPE_UINT32:
            return ES_PRECISION_UINT32;
        case ES_NPU_DATA_TYPE_INT64:
            return ES_PRECISION_INT64;
        case ES_NPU_DATA_TYPE_UINT64:
            return ES_PRECISION_UINT64;
        case ES_NPU_DATA_TYPE_UNKNOWN:
        default:
            printf("data type:%d not support\n", dataType);
            return ES_PRECISION_UNKNOWN;
    }
}

int32_t EsPostProcess::convertPrecisionToBytes(ES_DATA_PRECISION_E precision)
{
    switch (precision) {
        case ES_PRECISION_UNKNOWN:
            return 0;
            break;
        case ES_PRECISION_INT8:
        case ES_PRECISION_UINT8:
            return 1;
            break;
        case ES_PRECISION_INT16:
        case ES_PRECISION_UINT16:
            return 2;
            break;
        case ES_PRECISION_INT32:
        case ES_PRECISION_UINT32:
            return 4;
            break;
        case ES_PRECISION_INT64:
        case ES_PRECISION_UINT64:
            return 8;
            break;
        case ES_PRECISION_FP16:
            return 2;
            break;
        case ES_PRECISION_FP32:
            return 4;
            break;
    }
    return 0;
}

int32_t EsPostProcess::prepareSysMem(ES_U64 &fd, uint64_t size)
{
    int32_t ret;
    void *pData;
    const char *memZone = (mDieId == 0) ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0";

    ret = ES_SYS_MemAlloc(&fd, SYS_CACHE_MODE_NOCACHE, "es_npu_postprocess", memZone, size);
    if (ES_SUCCESS != ret) {
        printf("ES_SYS_MemAlloc failed, ret: %d\n", ret);
        return -1;
    }

    pData = ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE);
    if (pData == NULL) {
        printf("ES_SYS_Mmap failed\n");
        ES_SYS_MemFree(fd);
        return -1;
    }

    (void)memset(pData, 0x00, size);

    ret = ES_SYS_Munmap(pData, size);
    if (ES_SUCCESS != ret) {
        printf("ES_SYS_Munmap failed, ret: %d\n", ret);
        ES_SYS_MemFree(fd);
        return -1;
    }

    return 0;
}

std::vector<std::vector<int32_t>> EsPostProcess::parseDataShape(std::string &lines)
{
    std::string str;
    std::vector<std::vector<int32_t>> result;
    std::vector<std::vector<std::string>> midResult;
    bool flag = false;

    for (auto &ch : lines) {
        if (ch == '[') {
            str.clear();
            flag = true;
        } else if (ch == ']') {
            midResult.push_back(split(str, ','));
            flag = false;
        } else {
            if (flag) str.push_back(ch);
        }
    }

    for (auto &strVec : midResult) {
        std::vector<int32_t> tmp;
        for (auto &s : strVec) {
            tmp.push_back(std::stoi(s));
        }
        result.push_back(tmp);
    }

    return result;
}

std::vector<int32_t> EsPostProcess::parseDataType(std::string &lines)
{
    std::vector<std::string> splitResult;
    std::vector<int32_t> result;

    splitResult = split(lines, ',');
    for (auto &str : splitResult) {
        result.push_back(DATATYPE_MAP.at(str));
    }

    return result;
}

template <typename T>
std::vector<T> EsPostProcess::parseArray(std::string &lines)
{
    std::vector<T> result;
    std::string str;

    for (auto &ch : lines) {
        if (ch != '[' && ch != ']') {
            str.push_back(ch);
        }
    }

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        std::stringstream val;
        val << token;
        T v;
        val >> v;
        result.push_back(v);
    }

    return result;
}

template <typename T>
std::vector<int32_t> EsPostProcess::extractBoxNum(void *data, int32_t count)
{
    std::vector<int32_t> result;
    T *pData = (T *)data;

    for (int32_t i = 0; i < count; i++) {
        result.push_back(pData[i]);
    }

    return result;
}

template <typename T>
std::vector<std::vector<std::vector<float>>> EsPostProcess::extractBoxInfo(void *data, std::vector<int32_t> &boxCnt,
                                                                           int32_t size)
{
    std::vector<std::vector<std::vector<float>>> result;
    T *pData = (T *)data;

    for (auto &cnt : boxCnt) {
        std::vector<std::vector<float>> boxVec;
        for (int32_t i = 0; i < cnt; i++) {
            std::vector<float> boxInfo;
            for (int32_t j = 0; j < size; j++) {
                boxInfo.push_back(*pData);
                pData++;
            }
            boxVec.push_back(boxInfo);
        }
        result.push_back(boxVec);
    }

    return result;
}

void EsPostProcess::setParams(POSTPROCESS_PARAMS &params) { mParams = params; }

int32_t EsPostProcess::drawPosePointToPicture(std::string &picPath, std::vector<ES_POSE_POINT> &poseResult,
                                              const std::string &outputDir)
{
    cv::Mat img = cv::imread(picPath);
    int32_t imgW = img.cols;
    int32_t imgH = img.rows;
    int32_t inputW = mRtmPoseConfigs.imgW;
    int32_t inputH = mRtmPoseConfigs.imgH;

    for (const auto &kp : poseResult) {
        if (kp.score > 0.5) {
            cv::Point2i center((kp.x * imgW / inputW), (kp.y * imgH / inputH));
            cv::circle(img, center, 1, cv::Scalar(0, 255, 0), -1);
        }
    }

    std::filesystem::path filePath(picPath);
    std::string fileName = filePath.filename().string();
    std::string saveFile = outputDir + "/result_" + fileName;
    imwrite(saveFile, img);

    return 0;
}

std::vector<std::vector<float>> EsPostProcess::convertDetectResultToOriginal(
    const std::string &picPath, const std::vector<std::vector<float>> &boxinfo)
{
    std::vector<std::vector<float>> oringalBoxs;
    cv::Mat img = cv::imread(picPath);
    if (img.empty()) {
        printf("Cannot read the image: %s\n", picPath.c_str());
        return oringalBoxs;
    }

    // Initialized with the data keepratio = false
    int newHeight = mDetectOutConfigs.imgH;
    int newWidth = mDetectOutConfigs.imgW;
    int startX = 0;
    int startY = 0;

    if (mParams.keepRatio) {
        // Calculate scaling factor
        double scale = static_cast<double>(mDetectOutConfigs.imgW) / std::max(img.rows, img.cols);

        // Calculate new dimensions
        newHeight = static_cast<int>(img.rows * scale);
        newWidth = static_cast<int>(img.cols * scale);

        if (!mParams.keepLongSide) {
            startX = (mDetectOutConfigs.imgW - newWidth) / 2;
            startY = (mDetectOutConfigs.imgH - newHeight) / 2;
        } else {
            // Padding, 0,0 means padding to Bottom or Right
            if (mParams.paddingPosition == "Top_Left") {
                if (img.cols > img.rows) {
                    // Top
                    startY = mDetectOutConfigs.imgH - newHeight;
                } else {
                    // Left
                    startX = mDetectOutConfigs.imgW - newWidth;
                }
            } else if (mParams.paddingPosition == "Half") {
                startX = (mDetectOutConfigs.imgW - newWidth) / 2;
                startY = (mDetectOutConfigs.imgH - newHeight) / 2;
            }
        }
    }

    for (auto &info : boxinfo) {
        std::vector<float> originlBox = info;
        originlBox[3] = (info[3] - startX) * img.cols / newWidth;
        originlBox[4] = (info[4] - startY) * img.rows / newHeight;
        originlBox[5] = (info[5] - startX) * img.cols / newWidth;
        originlBox[6] = (info[6] - startY) * img.rows / newHeight;
        oringalBoxs.emplace_back(originlBox);
    }

    return oringalBoxs;
}

int32_t EsPostProcess::getDetectResultImg(const std::string &picPath, const std::vector<std::vector<float>> &boxinfo,
                                          cv::Mat &resultImg)
{
    if (picPath.empty()) {
        printf("The picture path is empty.\n");
        return -1;
    }

    auto oringnalBoxs = convertDetectResultToOriginal(picPath, boxinfo);
    if (oringnalBoxs.empty() && !boxinfo.empty()) {
        printf("Failed to convert the detection to the original image.\n");
        return -1;
    }

    // font setting
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.5;
    int thickness = 1;
    cv::Scalar textColor(255, 0, 0);
    int baseline = 0;

    for (auto &info : oringnalBoxs) {
        cv::Point topLeft(info[3], info[4]);
        cv::Point bottomRight(info[5], info[6]);
        cv::Scalar color(0, 255, 0);  // green
        cv::rectangle(resultImg, topLeft, bottomRight, color, 1.8);

        std::string label = getLabelByIdx(int(info[1]));
        if (label.empty()) {
            printf("drawObjectToPicture:label is empty\n");
            return -1;
        }

        label = label.substr(label.find_first_not_of(" \t\r\n"),
                             label.find_last_not_of(" \t\r\n") - label.find_first_not_of(" \t\r\n") + 1);
        if (label.find('(') == 0 && label.find(')') == label.length() - 1) {
            label = label.substr(1, label.length() - 2);
        }
        if (label.front() == '\'' && label.back() == '\'') {
            label = label.substr(1, label.length() - 2);
        }

        std::stringstream ss;
        ss << label << " : " << std::fixed << std::setprecision(3) << info[2];
        std::string text = ss.str();

        baseline = 0;
        cv::Size textSize = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        cv::Point textOrg(topLeft.x, topLeft.y - 10);
        if (textOrg.y - textSize.height < 0) {
            textOrg.y = topLeft.y + textSize.height + 10;
        }
        if (textOrg.x + textSize.width > resultImg.cols) {
            textOrg.x = resultImg.cols - textSize.width - 10;
        }
        cv::putText(resultImg, text, textOrg, fontFace, fontScale, textColor, thickness);
    }

    return 0;
}

int32_t EsPostProcess::drawObjectToPicture(const std::string &picPath, const std::vector<std::vector<float>> &boxinfo,
                                           const std::string &outputDir)
{
    if (outputDir.empty() || picPath.empty()) {
        printf("Invalid input.\n");
        return -1;
    }

    cv::Mat img = cv::imread(picPath);
    if (img.empty()) {
        printf("Cannot read the image: %s\n", picPath.c_str());
        return -1;
    }

    int32_t ret = getDetectResultImg(picPath, boxinfo, img);
    if (ret) {
        printf("Failed to get detect result of image: %s\n", picPath.c_str());
        return -1;
    }

    std::filesystem::path filePath(picPath);
    std::string fileName = filePath.filename().string();
    std::string saveFile = outputDir + "/result_" + fileName;
    imwrite(saveFile, img);

    return 0;
}

int32_t EsPostProcess::writeClassesInfoToFile(std::string &picPath, std::vector<int32_t> &topKIndex,
                                              std::vector<float> &topKConfidence, const std::string &outputDir)
{
    std::string fileName;

    fileName = outputDir + "/prediction.txt";
    std::ofstream outFile(fileName, std::ios::app);
    if (!outFile.is_open()) {
        printf("open output file failed: %s\n", fileName.c_str());
        return -1;
    }

    outFile << picPath << std::endl;

    for (int32_t i = 0; i < topKIndex.size(); i++) {
        outFile << topKIndex[i] << " " << topKConfidence[i] << std::endl;
    }

    outFile.close();

    return 0;
}

int32_t EsPostProcess::writeDetectResultToFile(const std::string &picPath,
                                               const std::vector<std::vector<float>> &boxinfo,
                                               const std::string &outputDir, const uint64_t detectTotalCount)
{
    std::string fileName;
    fileName = outputDir + "/prediction.json";
    std::ofstream outFile(fileName, std::ios::app);
    if (!outFile.is_open()) {
        printf("open output file failed: %s\n", fileName.c_str());
        return -1;
    }

    if (mProcessCount == 0) {
        outFile << "[";
    }
    mProcessCount++;

    std::string imageIdStr = picPath.substr(picPath.find_last_of('/') + 1);
    auto oringnalBoxs = convertDetectResultToOriginal(picPath, boxinfo);
    if (oringnalBoxs.empty() && !boxinfo.empty()) {
        printf("Failed to convert the detection to the original image.\n");
        return -1;
    }

    for (auto &box : oringnalBoxs) {
        auto detectData = cJSON_CreateObject();
        cJSON_AddNumberToObject(detectData, "category_id", box[1]);
        cJSON_AddStringToObject(detectData, "image_id", imageIdStr.c_str());
        auto bboxObject = cJSON_CreateArray();
        cJSON_AddItemToArray(bboxObject, cJSON_CreateNumber((box[5] - box[3]) / 2 + box[3]));
        cJSON_AddItemToArray(bboxObject, cJSON_CreateNumber((box[6] - box[4]) / 2 + box[4]));
        cJSON_AddItemToArray(bboxObject, cJSON_CreateNumber(box[5] - box[3]));
        cJSON_AddItemToArray(bboxObject, cJSON_CreateNumber(box[6] - box[4]));
        cJSON_AddItemToObject(detectData, "bbox", bboxObject);
        cJSON_AddNumberToObject(detectData, "score", box[2]);

        char *data = cJSON_Print(detectData);
        outFile << data;
        if (mProcessCount == detectTotalCount && &box == &(oringnalBoxs.back())) {
            outFile << "]";
        } else {
            outFile << ",";
        }
        free(data);
        data = NULL;
        cJSON_Delete(detectData);
    }

    outFile.close();
    return 0;
}

int32_t EsPostProcess::writePosePointResultToFile(std::string &picPath, std::vector<ES_POSE_POINT> &poseResult,
                                                  const std::string &outputDir, const uint64_t taskCount)
{
    std::string fileName;
    fileName = outputDir + "/prediction.json";
    std::ofstream outFile(fileName, std::ios::app);
    if (!outFile.is_open()) {
        printf("open output file failed: %s\n", fileName.c_str());
        return -1;
    }

    if (mProcessCount == 0) {
        outFile << "[";
    }
    mProcessCount++;

    cv::Mat img = cv::imread(picPath);
    int32_t imgW = img.cols;
    int32_t imgH = img.rows;
    int32_t inputW = mRtmPoseConfigs.imgW;
    int32_t inputH = mRtmPoseConfigs.imgH;
    std::filesystem::path filePath(picPath);
    std::filesystem::path stem = filePath.stem();
    std::string picName = stem.filename().string();

    auto pointData = cJSON_CreateObject();
    cJSON_AddStringToObject(pointData, "img_id", picName.c_str());
    auto keyPObject = cJSON_CreateArray();

    for (const auto &kp : poseResult) {
        auto pObject = cJSON_CreateArray();
        cJSON_AddItemToArray(pObject, cJSON_CreateNumber(kp.x * imgW / inputW));
        cJSON_AddItemToArray(pObject, cJSON_CreateNumber(kp.y * imgH / inputH));
        cJSON_AddItemToArray(pObject, cJSON_CreateNumber(kp.score));
        cJSON_AddItemToArray(keyPObject, pObject);
    }

    cJSON_AddItemToObject(pointData, "keypoints", keyPObject);
    cJSON_AddItemToObject(pointData, "score", cJSON_CreateNumber(1.0));

    char *data = cJSON_Print(pointData);
    outFile << data;
    if (mProcessCount == taskCount) {
        outFile << "]";
    } else {
        outFile << ",";
    }
    free(data);
    data = NULL;
    cJSON_Delete(pointData);

    outFile.close();
    return 0;
}