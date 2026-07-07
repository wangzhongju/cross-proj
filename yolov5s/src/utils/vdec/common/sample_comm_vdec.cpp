// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>
#include "sample_comm.h"

#define FILE_WRITE_RETRY_TIME (50)

VB_SOURCE_E gVdecVBSource = VB_SOURCE_MODULE;

VB_POOL gPicVbPool[ES_VB_MAX_POOLS] = {ES_VB_INVALID_POOLID};

#define PRINTF_VDEC_GRP_STATUS(Grp, status)                                                                           \
    do {                                                                                                              \
        SAMPLE_PRT(                                                                                                   \
            "\033[0;33m-------------------------------------------------------------------------------\033[0;39m\n"); \
        SAMPLE_PRT("\033[0;33mgrp:%d, Type:%d, bStart:%d, DecodeFrames:%u, LeftPics:%u, LeftBytes:%u, "               \
                   "LeftFrames:%u, "                                                                                  \
                   "RecvFrames:%u\033[0;39m\n",                                                                       \
                   Grp, status.type, status.bStartRecvStream, status.decodeStreamFrames, status.leftPics,             \
                   status.leftStreamBytes, status.leftStreamFrames, status.recvStreamFrames);                         \
        SAMPLE_PRT(                                                                                                   \
            "\033[0;33mFormatErr:%d, picSizeErrSet:%d, streamUnsprt:%d, packErr:%d,"                                  \
            "prtclNumErrSet:%d,  vdecHardwareErr:%d,  picBufSizeErrSet:%d, vdecStreamNotRelease: %d\033[0;39m\n",     \
            status.vdecDecErr.formatErr, status.vdecDecErr.picSizeErrSet, status.vdecDecErr.streamUnsprt,             \
            status.vdecDecErr.packErr, status.vdecDecErr.prtclNumErrSet, status.vdecDecErr.vdecHardwareErr,           \
            status.vdecDecErr.picBufSizeErrSet, status.vdecDecErr.vdecStreamNotRelease);                              \
        SAMPLE_PRT(                                                                                                   \
            "\033[0;33m-------------------------------------------------------------------------------\033[0;39m\n"); \
    } while (0)

ES_S32 SAMPLE_COMM_VDEC_InitVBPool(ES_S32 grpNum, SAMPLE_VDEC_ATTR *sampleVdec)
{
    VB_CONFIG_S vbConf;
    ES_S32 i, j, pos = 0, ret;
    ES_BOOL bFindFlag;
    SAMPLE_VDEC_BUF sampleVdecBuf[ES_VDEC_MAX_GRP_NUM];
    VB_POOL_CONFIG_S vbPoolCfg;

    memset(sampleVdecBuf, 0, sizeof(SAMPLE_VDEC_BUF) * ES_VDEC_MAX_GRP_NUM);
    memset(&vbConf, 0, sizeof(VB_CONFIG_S));

    for (i = 0; i < grpNum; i++) {
        for (j = 0; j < ES_VDEC_OUT_CHN_NUM; j++) {
            if (!sampleVdec[i].enableChn[j]) {
                continue;
            }
            sampleVdecBuf[i].picBufSize += VDEC_GetPicBufferSize(sampleVdec[i].chnMode[j].pixelFormat,
                                                                 sampleVdec[i].width, sampleVdec[i].height, 1);
        }
    }

    /* PicBuffer */
    for (j = 0; j < VB_MAX_COMM_POOLS; j++) {
        bFindFlag = ES_FALSE;
        for (i = 0; i < grpNum; i++) {
            if ((ES_FALSE == bFindFlag) && (0 != sampleVdecBuf[i].picBufSize) &&
                (ES_FALSE == sampleVdecBuf[i].bPicBufAlloc)) {
                vbConf.poolCfgs[j].blkSize = sampleVdecBuf[i].picBufSize;
                vbConf.poolCfgs[j].blkCnt = sampleVdec[i].frameBufCnt;
                snprintf(vbConf.poolCfgs[j].mmzName, sizeof(vbConf.poolCfgs[j].mmzName), "%s",
                         SAMPLE_COMMON_GetVbName(sampleVdec[i].nId));
                sampleVdecBuf[i].bPicBufAlloc = ES_TRUE;
                bFindFlag = ES_TRUE;
                pos = j;
            }

            if ((ES_TRUE == bFindFlag) && (ES_FALSE == sampleVdecBuf[i].bPicBufAlloc) &&
                (vbConf.poolCfgs[j].blkSize == sampleVdecBuf[i].picBufSize) &&
                (!strcmp(vbConf.poolCfgs[j].mmzName, SAMPLE_COMMON_GetVbName(sampleVdec[i].nId)))) {
                vbConf.poolCfgs[j].blkCnt += sampleVdec[i].frameBufCnt;
                sampleVdecBuf[i].bPicBufAlloc = ES_TRUE;
            }
        }
    }

    vbConf.poolCnt = pos + 1;

    if (VB_SOURCE_MODULE == gVdecVBSource) {
        CHECK_RET(ES_VB_SetModPoolConfig(VB_UID_VDEC, &vbConf), "ES_VB_SetModPoolConfig");
        ret = ES_VB_ModPoolInit(VB_UID_VDEC);
        if (ES_SUCCESS != ret) {
            SAMPLE_PRT("ES_VB_ModPoolInit fail for 0x%x\n", ret);
            ES_VB_ModPoolExit(VB_UID_VDEC);
            return ES_FAILURE;
        }
    } else if (VB_SOURCE_USER == gVdecVBSource) {
        for (i = 0; i < grpNum; i++) {
            if ((0 != sampleVdecBuf[i].picBufSize) && (0 != sampleVdec[i].frameBufCnt)) {
                memset(&vbPoolCfg, 0, sizeof(VB_POOL_CONFIG_S));
                vbPoolCfg.blkSize = sampleVdecBuf[i].picBufSize;
                vbPoolCfg.blkCnt = sampleVdec[i].frameBufCnt;
                vbPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
                ES_VB_CreatePool(&vbPoolCfg, &gPicVbPool[i]);
                if (ES_VB_INVALID_POOLID == gPicVbPool[i]) {
                    goto fail;
                }
            }
        }
    }

    return ES_SUCCESS;

fail:
    for (; i >= 0; i--) {
        if (ES_VB_INVALID_POOLID != gPicVbPool[i]) {
            ret = ES_VB_DestroyPool(gPicVbPool[i]);
            if (ES_SUCCESS != ret) {
                SAMPLE_PRT("ES_VB_DestroyPool %d fail!\n", gPicVbPool[i]);
            }
            gPicVbPool[i] = ES_VB_INVALID_POOLID;
        }
    }
    return ES_FAILURE;
}

ES_VOID SAMPLE_COMM_VDEC_ExitVBPool(ES_VOID)
{
    ES_S32 i, ret;

    if (VB_SOURCE_MODULE == gVdecVBSource) {
        ES_VB_ModPoolExit(VB_UID_VDEC);
    } else if (VB_SOURCE_USER == gVdecVBSource) {
        for (i = ES_VB_MAX_POOLS - 1; i >= 0; i--) {
            if (ES_VB_INVALID_POOLID != gPicVbPool[i]) {
                ret = ES_VB_DestroyPool(gPicVbPool[i]);
                if (ES_SUCCESS != ret) {
                    SAMPLE_PRT("ES_VB_DestroyPool %d fail!\n", gPicVbPool[i]);
                }
                gPicVbPool[i] = ES_VB_INVALID_POOLID;
            }
        }
    }

    return;
}

ES_S32 SAMPLE_COMM_VDEC_Start(ES_S32 devId, ES_S32 grpNum, SAMPLE_VDEC_ATTR *sampleVdec)
{
    ES_S32 i, j;
    VDEC_GRP_ATTR_S grpAttr[ES_VDEC_MAX_GRP_NUM];
    VDEC_GRP_POOL_S pool;
    VDEC_GRP_PARAM_S grpParam;
    VDEC_MOD_PARAM_S modParam;
    memset(grpAttr, 0, sizeof(VDEC_GRP_ATTR_S));

    CHECK_RET(ES_VDEC_GetModParam(&modParam), "ES_VDEC_GetModParam");

    modParam.vdecVBSource = gVdecVBSource;
    CHECK_RET(ES_VDEC_SetModParam(&modParam), "ES_VDEC_GetModParam");

    for (i = 0; i < grpNum; i++) {
        grpAttr[i].type = sampleVdec[i].type;
        grpAttr[i].mode = sampleVdec[i].mode;
        grpAttr[i].picWidth = sampleVdec[i].width;
        grpAttr[i].picHeight = sampleVdec[i].height;
        grpAttr[i].streamBufSize = sampleVdec[i].width * sampleVdec[i].height;
        grpAttr[i].frameBufCnt = sampleVdec[i].frameBufCnt;
        grpAttr[i].align = 1;
        for (j = 0; j < ES_VDEC_OUT_CHN_NUM; j++) {
            if (sampleVdec[i].enableChn[j]) {
                grpAttr[i].frameBufSize += VDEC_GetPicBufferSize(sampleVdec[i].chnMode[j].pixelFormat,
                                                                 sampleVdec[i].width, sampleVdec[i].height, 1);
            }
        }
        if (PT_H264 == sampleVdec[i].type || PT_H265 == sampleVdec[i].type) {
            grpAttr[i].vdecVideoAttr.refFrameNum = sampleVdec[i].sampleVdecVideo.refFrameNum;
        } else if (PT_JPEG == sampleVdec[i].type || PT_MJPEG == sampleVdec[i].type) {
            grpAttr[i].mode = VIDEO_MODE_FRAME;
        }

        CHECK_GRP_RET(ES_VDEC_CreateGrp(i, sampleVdec[i].nId, &grpAttr[i]), i, "ES_VDEC_CreateGrp");

        if (VB_SOURCE_USER == gVdecVBSource) {
            pool.picVbPool = gPicVbPool[i];
            CHECK_GRP_RET(ES_VDEC_AttachVbPool(i, &pool), i, "ES_VDEC_AttachVbPool");
        }

        CHECK_GRP_RET(ES_VDEC_GetGrpParam(i, &grpParam), i, "ES_VDEC_GetGrpParam");
        if (PT_H264 == sampleVdec[i].type || PT_H265 == sampleVdec[i].type) {
            grpParam.vdecVideoParam.decMode = sampleVdec[i].sampleVdecVideo.decMode;
            if (VIDEO_DEC_MODE_IPB == grpParam.vdecVideoParam.decMode) {
                grpParam.vdecVideoParam.outputOrder = VIDEO_OUTPUT_ORDER_DISP;
            } else {
                grpParam.vdecVideoParam.outputOrder = VIDEO_OUTPUT_ORDER_DEC;
            }
        }
        grpParam.displayFrameNum = sampleVdec[i].displayFrameNum;
        CHECK_GRP_RET(ES_VDEC_SetGrpParam(i, &grpParam), i, "ES_VDEC_GetGrpParam");
        for (j = 0; j < ES_VDEC_OUT_CHN_NUM; j++) {
            if (sampleVdec[i].enableChn[j]) {
                CHECK_CHN_RET(ES_VDEC_EnableChn(i, j), i, "ES_VDEC_EnableChn");
                VDEC_CHN_MODE_S chnMode;
                memset(&chnMode, 0, sizeof(VDEC_CHN_MODE_S));
                // set decode pixelFormat
                chnMode.pixelFormat = sampleVdec[i].chnMode[j].pixelFormat;
                chnMode.alpha = sampleVdec[i].chnMode[j].alpha;
                chnMode.colorGamut = sampleVdec[i].chnMode[j].colorGamut;
                CHECK_CHN_RET(ES_VDEC_SetChnMode(i, j, &chnMode), i, "ES_VDEC_SetChnMode");
            }
        }

        CHECK_GRP_RET(ES_VDEC_StartRecvStream(i), i, "ES_VDEC_StartRecvStream");
    }

    return ES_SUCCESS;
}

ES_S32 SAMPLE_COMM_VDEC_Stop(ES_S32 grpNum)
{
    ES_S32 i;

    for (i = 0; i < grpNum; i++) {
        CHECK_GRP_RET(ES_VDEC_StopRecvStream(i), i, "ES_VDEC_StopRecvStream");
        CHECK_GRP_RET(ES_VDEC_DestroyGrp(i), i, "ES_VDEC_DestroyGrp");
    }

    return ES_SUCCESS;
}

ES_S32 SAMPLE_COMM_VDEC_Init() { return ES_VDEC_Init(); }

ES_S32 SAMPLE_COMM_VDEC_Deinit() { return ES_VDEC_Deinit(); }
