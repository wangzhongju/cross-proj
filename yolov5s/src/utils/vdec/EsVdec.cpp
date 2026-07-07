// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include "EsVdec.h"
#include <fstream>
#include <chrono>
#include <filesystem>
#include <vector>
#include <algorithm>
#include "es_memcp.h"
#include "sample_npu_utils.h"

std::atomic<bool> EsVdec::mInit{false};

EsVdec::EsVdec(uint16_t devId)
    : mGrpCount(1), mGrpId(devId), mChnId(0), mBufferCapacity(10), mDieId(devId), memoryPool(mBufferCapacity, mDieId)
{
}

EsVdec::~EsVdec() { deInitVdec(); }

int32_t EsVdec::initVdec(int32_t dienum)
{
    mGrpCount = dienum;
    if (!mInit.load()) {
        mInit.store(true);
        ES_S32 i;
        ES_S32 ret = ES_SUCCESS;
        SAMPLE_VDEC_ATTR sampleVdec[ES_VDEC_MAX_GRP_NUM];
        memset(sampleVdec, 0, sizeof(sampleVdec));

        /************************************************
        step1:  init SYS, init vdec
        *************************************************/
        ret = ES_SYS_Init();
        if (ES_SUCCESS != ret) {
            DEBUG_ERROR("ES_SYS_Init failed!\n");
            goto END1;
        }

        ret = SAMPLE_COMM_VDEC_Init();
        if (ES_SUCCESS != ret) {
            DEBUG_ERROR("SAMPLE_COMM_VDEC_Init failed!\n");
            goto END2;
        }

        /************************************************
        step2:  init module VB or user VB(for VDEC)
        *************************************************/
        for (i = 0; i < mGrpCount; i++) {
            sampleVdec[i].nId = i;
            sampleVdec[i].dieNum = dienum;
            sampleVdec[i].type = PT_JPEG;
            sampleVdec[i].width = MAX_WIDTH;
            sampleVdec[i].height = MAX_HEIGHT;
            sampleVdec[i].mode = VIDEO_MODE_FRAME;
            sampleVdec[i].sampleVdecVideo.refFrameNum = 3;
            sampleVdec[i].displayFrameNum = 2;
            sampleVdec[i].frameBufCnt = sampleVdec[i].displayFrameNum + 1;
            sampleVdec[i].enableChn[0] = ES_TRUE;
            sampleVdec[i].chnMode[0].pixelFormat = PIXEL_FORMAT_B8G8R8;
            // sampleVdec[i].align = 64;
        }

        ret = SAMPLE_COMM_VDEC_InitVBPool(mGrpCount, &sampleVdec[0]);
        if (ret != ES_SUCCESS) {
            DEBUG_ERROR("init mod common vb fail for %#x!\n", ret);
            goto END3;
        }

        /************************************************
        step3:  start VDEC
        *************************************************/
        ret = SAMPLE_COMM_VDEC_Start(mDieId, mGrpCount, &sampleVdec[0]);
        if (ret != ES_SUCCESS) {
            DEBUG_ERROR("start VDEC fail for %#x!\n", ret);
            goto END4;
        }

        ret = SAMPLE_COMM_VPS_Init();
        if (ES_SUCCESS != ret) {
            DEBUG_ERROR("SAMPLE_COMM_VPS_Init failed with %#x!\n", ret);
            goto END5;
        }

        ret = ES_CmdQueue_Create(&mCmdQHandle);
        if (ret) {
            printf("ES_CmdQ_Create fail\n");
            goto END5;
        }
    }

    mRunning = true;

    return ES_SUCCESS;
END5:
    SAMPLE_COMM_VPS_Deinit();

END4:
    SAMPLE_COMM_VDEC_Stop(mGrpCount);

END3:
    SAMPLE_COMM_VDEC_ExitVBPool();

END2:
    SAMPLE_COMM_VDEC_Deinit();

END1:
    ES_SYS_Exit();

    return ES_FAILURE;
}

int32_t EsVdec::decodeJpeg(const JPEG_DATA &jpg)
{
    VDEC_STREAM_S stream;
    ES_U64 PTS = 0;
    ES_S32 ret;

    {
        std::unique_lock<std::mutex> lock(mVideoMutex);
        mVideoCond.wait(lock, [this] { return (mVideoQueue.size() < (size_t)(mBufferCapacity)) || !mRunning; });
        if (!mRunning) {
            DEBUG_INFO("%s: stop avsync\n", __func__);
            return ES_FAILURE;
        }
    }

    stream.PTS = PTS;
    stream.pAddr = (ES_U8 *)jpg.data;
    stream.len = jpg.len;
    stream.bEndOfFrame = ES_TRUE;
    stream.bEndOfStream = ES_FALSE;
    stream.bDisplay = ES_TRUE;

    VIDEO_FRAME_INFO_S midVideoInfoFrame;
    memset(&midVideoInfoFrame, 0, sizeof(VIDEO_FRAME_INFO_S));

    ret = ES_VDEC_SendStream(mGrpId, &stream, -1);
    if (ES_SUCCESS != ret) {
        DEBUG_ERROR("ES_VDEC_SendStream failed\n");
        return ES_FAILURE;
    }

    ret = ES_VDEC_GetFrame(mGrpId, mChnId, &midVideoInfoFrame, -1);
    if (ES_SUCCESS == ret) {
        // FILE *fp = fopen("dump111.rgb", "w");
        // SAMPLE_COMM_WriteYUV(&frame, fp, 1, 1);
        // fclose(fp);
    } else {
        DEBUG_ERROR("ES_VDEC_GetFrame failed\n");
        return ES_FAILURE;
    }

    // Save frame into the memory pool
    VDEC_VIDEO_FRAME frame;
    memoryPool.allocate(frame.videoInfoFrame);
    frame.name = jpg.name;

    uint64_t length = midVideoInfoFrame.videoFrame.width * midVideoInfoFrame.videoFrame.height * 3;

    frame.videoInfoFrame.videoFrame.width = midVideoInfoFrame.videoFrame.width;
    frame.videoInfoFrame.videoFrame.height = midVideoInfoFrame.videoFrame.height;
    frame.videoInfoFrame.videoFrame.stride[0] = midVideoInfoFrame.videoFrame.stride[0];
    frame.videoInfoFrame.videoFrame.stride[1] = midVideoInfoFrame.videoFrame.stride[1];
    frame.videoInfoFrame.videoFrame.stride[2] = midVideoInfoFrame.videoFrame.stride[2];
    frame.videoInfoFrame.videoFrame.offset[0] = midVideoInfoFrame.videoFrame.offset[0];
    frame.videoInfoFrame.videoFrame.offset[1] = midVideoInfoFrame.videoFrame.offset[1];
    frame.videoInfoFrame.videoFrame.offset[2] = midVideoInfoFrame.videoFrame.offset[2];

    DEBUG_INFO("Image Width:%d, Image Height:%d\n", frame.videoInfoFrame.videoFrame.width,
               frame.videoInfoFrame.videoFrame.height);

    DEBUG_INFO("stride 0:%d, stride 1:%d, stride 2:%d\n", frame.videoInfoFrame.videoFrame.stride[0],
               frame.videoInfoFrame.videoFrame.stride[1], frame.videoInfoFrame.videoFrame.stride[2]);

    DEBUG_INFO("offset 0:%d, offset 1:%d, offset 2:%d\n", frame.videoInfoFrame.videoFrame.offset[0],
               frame.videoInfoFrame.videoFrame.offset[1], frame.videoInfoFrame.videoFrame.offset[2]);

    ret = ES_Memcpy_Async(mCmdQHandle, midVideoInfoFrame.videoFrame.fd, 0, frame.videoInfoFrame.videoFrame.fd, 0,
                          length, 5000);
    if (ret) {
        DEBUG_ERROR("ES_Memcpy_Async cmdQHandle failed\n");
        ES_VDEC_ReleaseFrame(mGrpId, mChnId, &midVideoInfoFrame);
        return -1;
    }

    ret = ES_CmdQueue_Sync(mCmdQHandle);
    if (ret) {
        DEBUG_ERROR("ES_CmdQueue_Sync failed\n");
        ES_VDEC_ReleaseFrame(mGrpId, mChnId, &midVideoInfoFrame);
        return -1;
    }

    ES_VDEC_ReleaseFrame(mGrpId, mChnId, &midVideoInfoFrame);

    {
        std::unique_lock<std::mutex> lock(mVideoMutex);
        mVideoQueue.emplace(frame);
        mVideoCond.notify_all();
    }

    return ES_SUCCESS;
}

bool EsVdec::getVideoFrame(VDEC_VIDEO_FRAME &frame)
{
    std::unique_lock<std::mutex> lock(mVideoMutex);
    mVideoCond.wait(lock, [this] { return !mVideoQueue.empty() || !mRunning; });
    if (!mRunning && mVideoQueue.empty()) {
        DEBUG_INFO("%s: stop getVideoFrame and video queue is empty\n", __func__);
        return false;
    }

    VDEC_VIDEO_FRAME vframe = mVideoQueue.front();
    frame = vframe;

    return true;
}

void EsVdec::releaseFrame(VDEC_VIDEO_FRAME &frame)
{
    // Release video frame
    std::unique_lock<std::mutex> lock(mVideoMutex);
    memoryPool.deallocate(frame.videoInfoFrame);
    mVideoQueue.pop();
    mVideoCond.notify_one();
}

void EsVdec::flush()
{
    mRunning = false;
    {
        std::unique_lock<std::mutex> lock(mVideoMutex);
        mVideoCond.notify_all();
    }
}

void EsVdec::deInitVdec()
{
    flush();
    if (mInit.load()) {
        mInit.store(false);
        DEBUG_INFO("EsVdec::deinitVdec\n");
        SAMPLE_COMM_VDEC_Stop(mGrpCount);
        SAMPLE_COMM_VDEC_ExitVBPool();
        SAMPLE_COMM_VDEC_Deinit();
        SAMPLE_COMM_VPS_Deinit();
        ES_SYS_Exit();

        ES_S32 ret = ES_CmdQueue_Destroy(mCmdQHandle);
        if (ret) {
            perror("ES_CmdQueue_Destroy failed");
        }
    }
}