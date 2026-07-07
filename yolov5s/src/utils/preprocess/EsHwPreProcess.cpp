// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <fstream>
#include <chrono>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cmath>
#include "es_memcp.h"
#include "sample_npu_utils.h"
#include "EsHwPreProcess.h"

#define ALIGN_16_BYTES 16
#define MAX_WIDTH 2160
#define MAX_HEIGHT 1280

std::atomic<bool> EsHwPreProcess::mInit{false};

EsHwPreProcess::EsHwPreProcess(ES_U16 devId) : mGrpCount(1), mGrpId(0), mChnId(0), mDieId(devId) {}

EsHwPreProcess::~EsHwPreProcess() { release(); }

bool EsHwPreProcess::initializeFromJson(uint32_t devId, const std::string &cfgFilePath)
{
    bool success = parsePreProcessParamsFromJson(cfgFilePath, mPreprocessParams);
    if (!success) return success;

    // set normalization params
    memset(&mNormParams, 0, sizeof(mNormParams));
    mNormParams.normalizationMode = VPS_NORMALIZATION_Z_SCORE;

    ES_FLOAT mean1, mean2, mean3;
    ES_FLOAT std1, std2, std3;
    ES_FLOAT scale;

    if (mPreprocessParams.norm) {
        scale = 255;
    } else {
        scale = 1;
    }

    mean1 = mPreprocessParams.meanValues[0] * scale;
    mean2 = mPreprocessParams.meanValues[1] * scale;
    mean3 = mPreprocessParams.meanValues[2] * scale;
    std1 = 1 / (mPreprocessParams.stdValues[0] * scale);
    std2 = 1 / (mPreprocessParams.stdValues[1] * scale);
    std3 = 1 / (mPreprocessParams.stdValues[2] * scale);

    // For RGB model input, ES_VPS_Normalization using src format "BGR", so here need change r & b
    mNormParams.meanValue.r = *((ES_U32 *)&mean3);
    mNormParams.meanValue.g = *((ES_U32 *)&mean2);
    mNormParams.meanValue.b = *((ES_U32 *)&mean1);
    mNormParams.stdReciprocal.r = *((ES_U32 *)&std3);
    mNormParams.stdReciprocal.g = *((ES_U32 *)&std2);
    mNormParams.stdReciprocal.b = *((ES_U32 *)&std1);

    ES_FLOAT reciprocal = 1 / mPreprocessParams.quantizationScale;
    mNormParams.stepReciprocal = *((ES_U32 *)&reciprocal);
    mIs16ByteAligned = (mPreprocessParams.cropWidth % 16 == 0 ? true : false);
    // mNormParams.bByPassQuantization = ES_TRUE;
    ES_S32 ret = ES_SUCCESS;
    PIXEL_FORMAT_E middFmt;
    SIZE_S middSize;

    if (!mInit.load()) {
        mInit.store(true);
        ES_S32 i;
        /************************************************
        init VPS
        *************************************************/
        ret = SAMPLE_COMM_VPS_Init();
        if (ES_SUCCESS != ret) {
            DEBUG_ERROR("SAMPLE_COMM_VPS_Init failed with %#x!\n", ret);
            return false;
        }

        memset(&mMiddFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
        middSize.width = MAX_WIDTH;    // mPreprocessParams.resizeShapeWidth;
        middSize.height = MAX_HEIGHT;  // mPreprocessParams.resizeShapeHeight;
        middFmt = PIXEL_FORMAT_R8G8B8;
        ret = SAMPLE_COMM_INIT_BUFFER(&mMiddFrame, middSize, middFmt, mDieId);
        if (ret < 0) {
            SAMPLE_PRT("init dstFrame buffer failed, ret: %d\n", ret);
            goto END1;
        }

        memset(&mDstFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
        middSize.width = MAX_WIDTH;
        middSize.height = MAX_HEIGHT;
        ret = SAMPLE_COMM_INIT_BUFFER(&mDstFrame, middSize, middFmt, mDieId);
        if (ret < 0) {
            SAMPLE_PRT("init dstFrame buffer failed, ret: %d\n", ret);
            goto END2;
        }
    }

    return success;
END2:
    SAMPLE_COMM_DEINIT_BUFFER(&mMiddFrame);
END1:
    SAMPLE_COMM_VPS_Deinit();

    return false;
}

int32_t EsHwPreProcess::preprocess(VIDEO_FRAME_INFO_S &frame, PREPROCESSED_DATA &outData)
{
    std::lock_guard<std::mutex> lock(mMtx);

    ES_S32 ret;
    ES_FLOAT scale;
    RECT_S srcRect, middRect;
    ES_S32 newHeight;
    ES_S32 newWidth;

    auto start_time = std::chrono::high_resolution_clock::now();

    mMiddFrame.videoFrame.height = mPreprocessParams.resizeShapeHeight;
    mMiddFrame.videoFrame.width = mPreprocessParams.resizeShapeWidth;
    memset(&middRect, 0, sizeof(middRect));

    if (mPreprocessParams.keepRatio) {
        if (mPreprocessParams.keepLongSide) {
            // fill whole image with fillColor
            RECT_S fillRect = {0, 0, mMiddFrame.videoFrame.width, mMiddFrame.videoFrame.height};
            ES_U32 fillColor = 0;
            if (mPreprocessParams.paddingData != 0) {
                fillColor = 0xFF000000 | (mPreprocessParams.paddingData << 16) | (mPreprocessParams.paddingData << 8) |
                            mPreprocessParams.paddingData;
            }
            ret = ES_VPS_Fill(&mMiddFrame.videoFrame, &fillColor, 1, &fillRect, 1, HW_TYPE_HAE);
            if (ret != ES_SUCCESS) {
                DEBUG_ERROR("ES_VPS_Fill failed, %d\n", ret);
            }

            // Resize based on the longer side
            scale = static_cast<ES_FLOAT>(
                        std::max(mPreprocessParams.resizeShapeWidth, mPreprocessParams.resizeShapeHeight)) /
                    std::max(frame.videoFrame.width, frame.videoFrame.height);
            if (frame.videoFrame.width > frame.videoFrame.height) {
                newHeight = static_cast<ES_S32>(round(frame.videoFrame.height * scale));
                newWidth = mPreprocessParams.resizeShapeWidth;
            } else {
                newHeight = mPreprocessParams.resizeShapeHeight;
                newWidth = static_cast<ES_S32>(round(frame.videoFrame.width * scale));
            }

            // Padding, 0,0 means padding to Bottom or Right
            if (mPreprocessParams.paddingPosition == "Top_Left") {
                if (frame.videoFrame.width > frame.videoFrame.height) {
                    // Top
                    middRect.y = mMiddFrame.videoFrame.height - newHeight;
                } else {
                    // Left
                    middRect.x = mMiddFrame.videoFrame.width - newWidth;
                }
            } else if (mPreprocessParams.paddingPosition == "Half") {
                middRect.x = (mMiddFrame.videoFrame.width - newWidth) / 2;
                middRect.y = (mMiddFrame.videoFrame.height - newHeight) / 2;
            }
        } else {
            // Resize based on the shorter side
            scale = static_cast<ES_FLOAT>(
                        std::min(mPreprocessParams.resizeShapeWidth, mPreprocessParams.resizeShapeHeight)) /
                    std::min(frame.videoFrame.width, frame.videoFrame.height);
            if (frame.videoFrame.height < frame.videoFrame.width) {
                newHeight = mPreprocessParams.resizeShapeHeight;
                newWidth = static_cast<ES_S32>(round(frame.videoFrame.width * scale));
            } else {
                newHeight = static_cast<ES_S32>(round(frame.videoFrame.height * scale));
                newWidth = mPreprocessParams.resizeShapeWidth;
            }
            mMiddFrame.videoFrame.height = newHeight;
            mMiddFrame.videoFrame.width = newWidth;
        }

        middRect.width = newWidth;
        middRect.height = newHeight;
    } else {
        middRect.height = mMiddFrame.videoFrame.height;
        middRect.width = mMiddFrame.videoFrame.width;
    }

    VPS_PROPERTY_S prop = {};
    prop.type = VPS_PROPERTY_RESIZE_METHOD;
    // VPS_RESIZE_STRETCHBLIT, VPS_RESIZE_FILTERBLIT, VPS_RESIZE_BILINEAR, VPS_RESIZE_BICUBIC
    prop.u.resizeMethod = VPS_RESIZE_BILINEAR;
    ret = ES_VPS_SetProperty(&prop);
    if (ret != ES_SUCCESS) {
        DEBUG_ERROR("Set resize method [%d] failed\n", prop.u.resizeMethod);
    }

    // resize frame(originW, originH) to middle frame, BGR -> BGR
    frame.videoFrame.pixelFormat = PIXEL_FORMAT_B8G8R8;
    mMiddFrame.videoFrame.pixelFormat = PIXEL_FORMAT_B8G8R8;
    ret = ES_VPS_CropResize(&frame.videoFrame, &mMiddFrame.videoFrame, ES_NULL, &middRect, 0, HW_TYPE_HAE);
    if (ret != ES_SUCCESS) {
        DEBUG_ERROR("ES_VPS_Resize failed, %d\n", ret);
    }

    // 2D not support convert to RGB, so we should use BGR input as RGB, convert it to BGR(actually is RGB -> RGB)
    if (mPreprocessParams.inputFormat == "RGB") {
        mMiddFrame.videoFrame.pixelFormat = PIXEL_FORMAT_R8G8B8;
    }

    VIDEO_FRAME_INFO_S dstFrame;
    memset(&dstFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
    dstFrame.videoFrame.fd = mIs16ByteAligned ? outData.fd : mDstFrame.videoFrame.fd;
    dstFrame.poolId = ES_VB_INVALID_POOLID;
    dstFrame.modId = ES_ID_USER;
    dstFrame.videoFrame.width = mPreprocessParams.cropWidth;
    dstFrame.videoFrame.height = mPreprocessParams.cropHeight;
    int32_t offset = (mIs16ByteAligned ? outData.offset : 0);
    if (mPreprocessParams.channelFirst) {
        dstFrame.videoFrame.pixelFormat = PIXEL_FORMAT_B8G8R8I_PLANAR;
        dstFrame.videoFrame.stride[0] = ES_ALIGN_UP(mPreprocessParams.cropWidth, ALIGN_16_BYTES);
        dstFrame.videoFrame.stride[1] = ES_ALIGN_UP(mPreprocessParams.cropWidth, ALIGN_16_BYTES);
        dstFrame.videoFrame.stride[2] = ES_ALIGN_UP(mPreprocessParams.cropWidth, ALIGN_16_BYTES);

        dstFrame.videoFrame.offset[0] = offset;
        dstFrame.videoFrame.offset[1] = offset + (ES_ALIGN_UP(mPreprocessParams.cropWidth, ALIGN_16_BYTES) *
                                                  ES_ALIGN_UP(mPreprocessParams.cropHeight, ALIGN_16_BYTES));
        dstFrame.videoFrame.offset[2] = offset + (ES_ALIGN_UP(mPreprocessParams.cropWidth, ALIGN_16_BYTES) *
                                                  ES_ALIGN_UP(mPreprocessParams.cropHeight, ALIGN_16_BYTES) * 2);
    } else {
        dstFrame.videoFrame.pixelFormat = PIXEL_FORMAT_B8G8R8I;
        dstFrame.videoFrame.stride[0] = ES_ALIGN_UP(mPreprocessParams.cropWidth * 3, ALIGN_16_BYTES);
        dstFrame.videoFrame.stride[1] = 0;
        dstFrame.videoFrame.stride[2] = 0;

        dstFrame.videoFrame.offset[0] = offset;
        dstFrame.videoFrame.offset[1] = 0;
        dstFrame.videoFrame.offset[2] = 0;
    }
    // printf("mPreprocessParams.cropWidth:%d, dstFrame.videoFrame.stride[0]:%d\n", mPreprocessParams.cropWidth,
    // dstFrame.videoFrame.stride[0]);
    memset(&srcRect, 0, sizeof(srcRect));
    srcRect.x = (mMiddFrame.videoFrame.width - dstFrame.videoFrame.width) / 2;
    srcRect.y = (mMiddFrame.videoFrame.height - dstFrame.videoFrame.height) / 2;
    srcRect.width = dstFrame.videoFrame.width;
    srcRect.height = dstFrame.videoFrame.height;

    DEBUG_INFO("src width:%d, height:%d\n", frame.videoFrame.width, frame.videoFrame.height);
    DEBUG_INFO("dst width:%d, height:%d\n", dstFrame.videoFrame.width, dstFrame.videoFrame.height);
    DEBUG_INFO("src rect: w:%d, h:%d, x:%d, y:%d\n", srcRect.width, srcRect.height, srcRect.x, srcRect.y);

    // Normalization
    ret = ES_VPS_Normalization(&mMiddFrame.videoFrame, &dstFrame.videoFrame, &mNormParams, &srcRect, NULL);
    if (ret != ES_SUCCESS) {
        DEBUG_ERROR("ES_VPS_Normalization return with 0x%x!\n", ret);
    }

    // Copy to NPU buffer if width is not 16 bytes align
    //////////////////////////////////////////////////////////////////////////////////
    if (!mIs16ByteAligned) {
        uint8_t *pOutData = (uint8_t *)ES_SYS_Mmap(outData.fd, outData.size + outData.offset, SYS_CACHE_MODE_NOCACHE);
        if (!pOutData) {
            printf("ES_SYS_Mmap failed!\n");
            return ES_FAILURE;
        }

        uint8_t *pInData =
            (uint8_t *)ES_SYS_Mmap(dstFrame.videoFrame.fd, MAX_WIDTH * MAX_HEIGHT * 3, SYS_CACHE_MODE_NOCACHE);
        if (!pInData) {
            printf("ES_SYS_Mmap failed!\n");
            ES_SYS_Munmap(pOutData, outData.size + outData.offset);
            return ES_FAILURE;
        }

        int32_t totalChannel = 3;
        int line = 0;
        for (int channel = 0; channel < totalChannel; channel++) {
            for (int i = 0; i < dstFrame.videoFrame.height; i++) {
                int srcOffset = dstFrame.videoFrame.offset[channel] + dstFrame.videoFrame.stride[channel] * i;
                int dstOffset = dstFrame.videoFrame.width * line;
                line++;
                memcpy(pOutData + outData.offset + dstOffset, pInData + srcOffset, dstFrame.videoFrame.width);
            }
        }

        ES_SYS_Munmap(pInData, MAX_WIDTH * MAX_HEIGHT * 3);
        ES_SYS_Munmap(pOutData, outData.size + outData.offset);
    }
    //////////////////////////////////////////////////////////////////////////////////

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end_time - start_time);

    DEBUG_INFO("EsHwPreProcess::%s Preprocess cost:%.4f\n", __func__, duration.count());

    return ret;
}

void EsHwPreProcess::release()
{
    if (mInit.load()) {
        mInit.store(false);
        DEBUG_INFO("EsHwPreProcess::release\n");
        SAMPLE_COMM_VPS_Deinit();
        SAMPLE_COMM_DEINIT_BUFFER(&mMiddFrame);
        SAMPLE_COMM_DEINIT_BUFFER(&mDstFrame);
    }
}
