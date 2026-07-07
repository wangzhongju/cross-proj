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
#include <atomic>

#include "sample_comm.h"

static std::atomic<bool> vpsInit{false};

#define PRINTF_VPS_GRP_STATUS(grp, status)                                                                             \
    do {                                                                                                               \
        SAMPLE_PRT("\nGroup[%d] status:\n"                                                                             \
                   "Recv[%u] = Reject[%u] + Accept[%u] + Drop[%u] + Fail[%u]\n"                                        \
                   "CHN0 OutputQueue[%u], Drop[%u], Fail[%u], Consumed[%u]\n"                                          \
                   "CHN1 OutputQueue[%u], Drop[%u], Fail[%u], Consumed[%u]\n"                                          \
                   "CHN2 OutputQueue[%u], Drop[%u], Fail[%u], Consumed[%u]\n\n",                                       \
                   grp, status.grpRecvFrmCnt, status.grpRejectFrmCnt, status.grpAcceptFrmCnt, status.grpDropFrmCnt,    \
                   status.grpFailFrmCnt, status.chnFrmStatus[0].chnOutputFrmCnt, status.chnFrmStatus[0].chnDropFrmCnt, \
                   status.chnFrmStatus[0].chnFailFrmCnt, status.chnFrmStatus[0].chnConsumedFrmCnt,                     \
                   status.chnFrmStatus[1].chnOutputFrmCnt, status.chnFrmStatus[1].chnDropFrmCnt,                       \
                   status.chnFrmStatus[1].chnFailFrmCnt, status.chnFrmStatus[1].chnConsumedFrmCnt,                     \
                   status.chnFrmStatus[2].chnOutputFrmCnt, status.chnFrmStatus[2].chnDropFrmCnt,                       \
                   status.chnFrmStatus[2].chnFailFrmCnt, status.chnFrmStatus[2].chnConsumedFrmCnt);                    \
    } while (0)

#define SELECT_TIMEOUT (2)

static const ES_CHAR* retCode2Char(ES_S32 returnCode)
{
    switch (returnCode) {
        case ES_ERR_VPS_NULL_PTR:
            return "ES_ERR_VPS_NULL_PTR";
        case ES_ERR_VPS_NOTREADY:
            return "ES_ERR_VPS_NOTREADY";
        case ES_ERR_VPS_INVALID_GRPID:
            return "ES_ERR_VPS_INVALID_GRPID";
        case ES_ERR_VPS_INVALID_CHNID:
            return "ES_ERR_VPS_INVALID_CHNID";
        case ES_ERR_VPS_EXIST:
            return "ES_ERR_VPS_EXIST";
        case ES_ERR_VPS_UNEXIST:
            return "ES_ERR_VPS_UNEXIST";
        case ES_ERR_VPS_NOT_SUPPORT:
            return "ES_ERR_VPS_NOT_SUPPORT";
        case ES_ERR_VPS_NOT_PERM:
            return "ES_ERR_VPS_NOT_PERM";
        case ES_ERR_VPS_NOMEM:
            return "ES_ERR_VPS_NOMEM";
        case ES_ERR_VPS_NOBUF:
            return "ES_ERR_VPS_NOBUF";
        case ES_ERR_VPS_ILLEGAL_PARAM:
            return "ES_ERR_VPS_ILLEGAL_PARAM";
        case ES_ERR_VPS_BUSY:
            return "ES_ERR_VPS_BUSY";
        case ES_ERR_VPS_BUF_EMPTY:
            return "ES_ERR_VPS_BUF_EMPTY";
        case ES_ERR_VPS_BUF_FULL:
            return "ES_ERR_VPS_BUF_FULL";
        case ES_SUCCESS:
            return "ES_SUCCESS";
        case ES_FAILURE:
            return "ES_FAILURE";
        default:
            return "VPS_UNKNOWN_ERROR";
    }
}

ES_S32 SAMPLE_COMM_INIT_BUFFER(VIDEO_FRAME_INFO_S* pFrame, SIZE_S size, PIXEL_FORMAT_E fmt, uint16_t devId)
{
    if (!pFrame) {
        SAMPLE_PRT("param is invalid, pframe[%p]!\n", pFrame);
        return ES_FAILURE;
    }
    ES_U64 blkSize = 0;
    const char* memZone = (devId == 0) ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0";

    pFrame->poolId = ES_VB_INVALID_POOLID;
    pFrame->modId = ES_ID_USER;
    pFrame->videoFrame.width = size.width;
    pFrame->videoFrame.height = size.height;
    pFrame->videoFrame.pixelFormat = fmt;
    blkSize = COMMON_GetPicBufferSize(fmt, size.width, size.height, 64, 8);
    ES_S32 ret = ES_SYS_MemAlloc(&pFrame->videoFrame.fd, SYS_CACHE_MODE_NOCACHE, ES_NULL, memZone, blkSize);
    if (ret < 0) {
        SAMPLE_PRT("memAlloc fd failed, ret: %d\n", ret);
        return ES_FAILURE;
    }
    return ES_SUCCESS;
}

ES_S32 SAMPLE_COMM_DEINIT_BUFFER(VIDEO_FRAME_INFO_S* pFrame)
{
    if (!pFrame) {
        SAMPLE_PRT("param is invalid, pframe[%p]!\n", pFrame);
        return ES_FAILURE;
    }
    if (pFrame->videoFrame.fd > 0) ES_SYS_MemFree(pFrame->videoFrame.fd);
    return ES_SUCCESS;
}

ES_S32 SAMPLE_COMM_VPS_Stop(VPS_GRP vpsGrp, ES_BOOL* pChnEnable, SAMPLE_VPS_CONFIG_S* pVpsConfig)
{
    ES_S32 ret = ES_SUCCESS;

    ret = ES_VPS_StopGrp(vpsGrp);
    if (ret != ES_SUCCESS) {
        SAMPLE_PRT("Group[%d] ES_VPS_StopGrp failed with %s!\n", vpsGrp, retCode2Char(ret));
        return ES_FAILURE;
    }

    if (pVpsConfig) {
        if (pVpsConfig->grpOverlay.bEnable && pVpsConfig->grpOverlay.handle) {
            ret = ES_VPS_DetachGrpOverlay(vpsGrp, pVpsConfig->grpOverlay.handle);
            if (ret != ES_SUCCESS) {
                SAMPLE_PRT("Group[%d] ES_VPS_DetachGrpOverlay failed with %s!\n", vpsGrp, retCode2Char(ret));
            }
            ES_VPS_DestroyOverlay(pVpsConfig->grpOverlay.handle);
        }
    }

    for (ES_S32 i = 0; i < ES_VPS_MAX_CHN_NUM; i++) {
        if (pChnEnable[i]) {
            if (pVpsConfig) {
                if (pVpsConfig->chnCfg[i].chnOverlay.bEnable && pVpsConfig->chnCfg[i].chnOverlay.handle) {
                    ret = ES_VPS_DetachChnOverlay(vpsGrp, i, pVpsConfig->chnCfg[i].chnOverlay.handle);
                    if (ret != ES_SUCCESS) {
                        SAMPLE_PRT("Group[%d] Channel[%d] ES_VPS_DetachChnOverlay failed with %s!\n", vpsGrp, i,
                                   retCode2Char(ret));
                    }
                    ES_VPS_DestroyOverlay(pVpsConfig->chnCfg[i].chnOverlay.handle);
                }
            }
            ret = ES_VPS_DisableChn(vpsGrp, i);
            if (ret != ES_SUCCESS) {
                SAMPLE_PRT("Group[%d] Channel[%d] ES_VPS_DisableChn failed with %s\n", vpsGrp, i, retCode2Char(ret));
                return ES_FAILURE;
            }
        }
    }

    ret = ES_VPS_DestroyGrp(vpsGrp);
    if (ret != ES_SUCCESS) {
        SAMPLE_PRT("Group[%d] ES_VPS_DestroyGrp failed with %s!\n", vpsGrp, retCode2Char(ret));
        return ES_FAILURE;
    }

    return ES_SUCCESS;
}

ES_S32 SAMPLE_COMM_VPS_Init()
{
    if (vpsInit.load()) return ES_SUCCESS;

    vpsInit.store(true);
    return ES_VPS_Init();
}

ES_S32 SAMPLE_COMM_VPS_Deinit()
{
    if (!vpsInit.load()) return ES_SUCCESS;

    vpsInit.store(false);
    return ES_VPS_Deinit();
}
