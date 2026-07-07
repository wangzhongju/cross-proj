// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _ES_POSTPROCESS_H
#define _ES_POSTPROCESS_H

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "es_nn_common.h"
#include "es_npu_types.h"
#include "es_ak_types.h"
#include "cJSON.h"

class DetectOutConfigs
{
 public:
    enum EsDetNetwork {
        ES_YOLOV3_U = 0,
        ES_YOLOV3_D = 1,
        ES_YOLOV4 = 2,
        ES_YOLOV5 = 3,
        ES_YOLOV7 = 4,
        ES_YOLOV8 = 5,
        ES_DAMOYOLO = 8
    };
    enum EsNmsMethod {
        ES_HARD_NMS = 0,
        ES_SOFT_NMS_GAUSSIAN,
        ES_SOFT_NMS_LINEAR
    };

    enum EsIouMethod {
        ES_IOU = 0,
        ES_GIOU,
        ES_DIOU
    };

    enum EsBoxType {
        ES_XMIN_YMIN_XMAX_YMAX = 0,
        ES_XMIN_YMIN_W_H,
        ES_YMIN_XMIN_YMAX_XMAX,
        ES_XMID_YMID_W_H,
    };

    EsDetNetwork detectNet;
    int32_t outTensorNum;
    int32_t inTensorNum;
    std::vector<std::vector<int32_t>> inputShape;
    std::vector<std::vector<int32_t>> outputShape;
    std::vector<int32_t> inputDataType;
    std::vector<int32_t> outputDataType;
    int32_t anchorsNum;
    std::vector<float> anchorScale;
    int32_t imgH;
    int32_t imgW;
    int32_t clsNum;
    std::vector<float> inputScale;
    int32_t nmsMethod;
    int32_t iouMethod;
    int32_t outBoxType;
    int32_t coordNorm;
    int32_t maxBoxesPerClass;
    int32_t maxBoxesPerBatch;
    float scoreThreshold;
    float iouThreshold;
    float softNmsSigma;
    float effecImgOffsetX;
    float effecImgOffsetY;
};

typedef struct {
    int32_t imgH;
    int32_t imgW;
    double scaleX;
    double scaleY;
} RtmPoseConfigs;

typedef struct {
    bool keepRatio;
    bool keepLongSide;
    std::string paddingPosition;
} POSTPROCESS_PARAMS;

class EsPostProcess
{
 public:
    explicit EsPostProcess(uint16_t dieId);
    virtual ~EsPostProcess();
    bool parseArgs(std::string fileName);
    int32_t getProcessType();
    int32_t getArgmaxK();
    ES_DATA_PRECISION_E convertDataType(uint8_t dataType);
    bool parseClassification(std::string fileName);
    std::string getLabelByIdx(int idx, bool full = false);
    std::vector<std::vector<float>> convertDetectResultToOriginal(const std::string &picPath,
                                                                  const std::vector<std::vector<float>> &boxinfo);
    int32_t getDetectResultImg(const std::string &picPath, const std::vector<std::vector<float>> &boxinfo,
                               cv::Mat &resultImg);
    int32_t drawObjectToPicture(const std::string &picPath, const std::vector<std::vector<float>> &boxinfo,
                                const std::string &outputDir);
    int32_t writeClassesInfoToFile(std::string &picPath, std::vector<int32_t> &topKIndex,
                                   std::vector<float> &topKConfidence, const std::string &outputDir);
    int32_t writeDetectResultToFile(const std::string &picPath, const std::vector<std::vector<float>> &boxinfo,
                                    const std::string &outputDir, const uint64_t detectTotalCount);
    int32_t classify(std::vector<ES_TENSOR_S> inTensors, std::vector<std::vector<int>> &topKIndex,
                     std::vector<std::vector<float>> &topKConfidence);
    int32_t detectionOut(std::vector<ES_TENSOR_S> inTensors, std::vector<std::vector<std::vector<float>>> &boxInfo);
    int32_t segment(std::vector<ES_TENSOR_S> inTensors, void *out);
    int32_t rtmPose(std::vector<ES_TENSOR_S> inTensors, std::vector<ES_POSE_POINT> &result);
    int32_t drawPosePointToPicture(std::string &picPath, std::vector<ES_POSE_POINT> &pose_result,
                                   const std::string &outputDir);
    int32_t writePosePointResultToFile(std::string &picPath, std::vector<ES_POSE_POINT> &poseResult,
                                       const std::string &outputDir, const uint64_t taskCount);

    void setParams(POSTPROCESS_PARAMS &params);

    typedef enum {
        CLASSIFY = 0,
        DETECTION_OUT,
        SEGMENT,
        RTMPOSE,
    } POST_PROCESS_TYPE;

 private:
    int32_t prepareSysMem(ES_U64 &fd, uint64_t size);
    void shapeCopy(ES_TENSOR_S &dst, ES_TENSOR_S &src);
    int32_t convertPrecisionToBytes(ES_DATA_PRECISION_E precision);
    std::vector<std::vector<int32_t>> parseDataShape(std::string &lines);
    std::vector<int32_t> parseDataType(std::string &lines);
    template <typename T>
    std::vector<T> parseArray(std::string &lines);
    template <typename T>
    std::vector<int32_t> extractBoxNum(void *data, int32_t count);
    template <typename T>
    std::vector<std::vector<std::vector<float>>> extractBoxInfo(void *data, std::vector<int32_t> &boxCnt, int32_t size);
    std::unordered_map<int, std::vector<std::string>> mLabelMap;
    std::string mProcessName;
    POST_PROCESS_TYPE mProcessType;
    int32_t mArgmaxK;
    double mScale;
    int32_t mStartIndex;
    int32_t mNumElements;
    DetectOutConfigs mDetectOutConfigs;
    RtmPoseConfigs mRtmPoseConfigs;
    static int32_t mInstanceCount;
    std::mutex mMtx;
    POSTPROCESS_PARAMS mParams;
    uint16_t mDieId;
    uint64_t mProcessCount;
};

#endif  // _ES_POSTPROCESS_H