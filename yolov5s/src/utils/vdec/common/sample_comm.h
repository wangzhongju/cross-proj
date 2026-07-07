// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __SAMPLE_COMM_H__
#define __SAMPLE_COMM_H__

#include <pthread.h>
#include <string>
#include <memory>

#include "es_common.h"
#include "es_defines.h"
#include "es_buffer.h"
#include "es_comm_sys.h"
#include "es_comm_vdec.h"
#include "es_comm_vps.h"

#include "es_sys.h"
#include "es_vb_memory.h"
#include "es_sys_memory.h"
#include "es_vdec.h"
#include "es_vps.h"
#include "es_math.h"

/*******************************************************
    macro define
*******************************************************/
#define FILE_NAME_LEN 128

#define CHECK_CHN_RET(express, Chn, name)                                                                       \
    do {                                                                                                        \
        ES_S32 Ret;                                                                                             \
        Ret = express;                                                                                          \
        if (ES_SUCCESS != Ret) {                                                                                \
            printf("\033[0;31m%s chn %d failed at %s: LINE: %d with %#x!\033[0;39m\n", name, Chn, __FUNCTION__, \
                   __LINE__, Ret);                                                                              \
            fflush(stdout);                                                                                     \
            return Ret;                                                                                         \
        }                                                                                                       \
    } while (0)

#define CHECK_RET(express, name)                                                                                    \
    do {                                                                                                            \
        ES_S32 Ret;                                                                                                 \
        Ret = express;                                                                                              \
        if (ES_SUCCESS != Ret) {                                                                                    \
            printf("\033[0;31m%s failed at %s: LINE: %d with %#x!\033[0;39m\n", name, __FUNCTION__, __LINE__, Ret); \
            return Ret;                                                                                             \
        }                                                                                                           \
    } while (0)
#define CHECK_GRP_RET CHECK_CHN_RET

#define COLOR_RGB_RED 0xFF0000
#define COLOR_RGB_GREEN 0x00FF00
#define COLOR_RGB_BLUE 0x0000FF
#define COLOR_RGB_BLACK 0x000000
#define COLOR_RGB_YELLOW 0xFFFF00
#define COLOR_RGB_CYN 0x00ffff
#define COLOR_RGB_WHITE 0xffffff

#define VB_MAX_COMM_POOLS 16

#define SAMPLE_PRT(fmt, args...)                                    \
    do {                                                            \
        printf("[%s]-%d: " fmt "", __FUNCTION__, __LINE__, ##args); \
    } while (0)

#define DEBUG_MODE 0
#if DEBUG_MODE
#define DEBUG_INFO(fmt, ...) printf("INFO: " fmt "", ##__VA_ARGS__)
#define DEBUG_ERROR(fmt, ...) printf("ERROR: " fmt "", ##__VA_ARGS__)
#else
#define DEBUG_INFO(fmt, ...)
#define DEBUG_ERROR(fmt, ...) printf("ERROR: " fmt "", ##__VA_ARGS__)
#endif
/*******************************************************
    structure define
*******************************************************/
typedef struct esSAMPLE_VDEC_BUF {
    ES_U32 picBufSize;
    ES_BOOL bPicBufAlloc;
} SAMPLE_VDEC_BUF;

typedef struct esSAMPLE_VDEC_VIDEO_ATTR {
    VIDEO_DEC_MODE_E decMode;
    ES_U32 refFrameNum;
} SAMPLE_VDEC_VIDEO_ATTR;

typedef struct esSAMPLE_VDEC_ATTR {
    DIE_IDX nId;
    ES_S32 dieNum;
    ES_S32 grpId;
    PAYLOAD_TYPE_E type;
    VIDEO_MODE_E mode;
    ES_U32 width;
    ES_U32 height;
    ES_U32 frameBufCnt;
    ES_U32 displayFrameNum;
    VDEC_CHN_MODE_S chnMode[ES_VDEC_OUT_CHN_NUM];
    ES_BOOL enableChn[ES_VDEC_OUT_CHN_NUM];
    SAMPLE_VDEC_VIDEO_ATTR sampleVdecVideo; /* structure with video (h265/h264) */
} SAMPLE_VDEC_ATTR;

typedef struct esSAMPLE_VPS_ROTATION_CFG_S {
    ES_BOOL bEnable;
    ROTATION_E rotation;
    HWTYPE_E type;
} SAMPLE_VPS_ROTATION_CFG_S;

typedef struct esSAMPLE_VPS_MULTI_CFG_S {
    ES_BOOL bEnable;
    VPS_MULTI_OUT_ATTR_S attr;
} SAMPLE_VPS_MULTI_CFG_S;

typedef struct esSAMPLE_VPS_DEWARP_CFG_S {
    ES_BOOL bEnable;
    VPS_DEWARP_PARAMS_S param;
} SAMPLE_VPS_DEWARP_CFG_S;

typedef struct esSAMPLE_VPS_OVERLAY_CFG_S {
    ES_BOOL bEnable;
    VPS_OVERLAY_HANDLE handle;
    VPS_OVERLAY_GROUP_S olGrps;
} SAMPLE_VPS_OVERLAY_CFG_S;

typedef struct esSAMPLE_VPS_CHN_CFG_S {
    SAMPLE_VPS_ROTATION_CFG_S rotation;
    SAMPLE_VPS_OVERLAY_CFG_S chnOverlay;
} SAMPLE_VPS_CHN_CFG_S;

typedef struct esSAMPLE_VPS_CONFIG_S {
    /* for group */
    SAMPLE_VPS_MULTI_CFG_S multi;
    SAMPLE_VPS_DEWARP_CFG_S dewarp;
    SAMPLE_VPS_OVERLAY_CFG_S grpOverlay;

    /* for channel */
    SAMPLE_VPS_CHN_CFG_S chnCfg[ES_VPS_MAX_CHN_NUM];
} SAMPLE_VPS_CONFIG_S;

/*******************************************************
    function announce
*******************************************************/
ES_VOID SAMPLE_COMM_SYS_Exit(ES_VOID);
ES_S32 SAMPLE_COMM_SYS_Init(VB_CONFIG_S *pVbConfig);

ES_S32 SAMPLE_COMM_VPS_Stop(VPS_GRP vpsGrp, ES_BOOL *pChnEnable, SAMPLE_VPS_CONFIG_S *pVpsConfig);
ES_S32 SAMPLE_COMM_VPS_Init();
ES_S32 SAMPLE_COMM_VPS_Deinit();

ES_S32 SAMPLE_COMM_VDEC_InitVBPool(ES_S32 grpNum, SAMPLE_VDEC_ATTR *pSampleVdec);
ES_VOID SAMPLE_COMM_VDEC_ExitVBPool(ES_VOID);
ES_S32 SAMPLE_COMM_VDEC_Start(ES_S32 devId, ES_S32 grpNum, SAMPLE_VDEC_ATTR *pSampleVdec);
ES_S32 SAMPLE_COMM_VDEC_Stop(ES_S32 grpNum);
ES_S32 SAMPLE_COMM_VDEC_Init();
ES_S32 SAMPLE_COMM_VDEC_Deinit();

ES_S32 SAMPLE_COMM_WriteYUV(VIDEO_FRAME_INFO_S *pFrame, FILE *pFile, ES_U32 align, ES_U32 alignH);
ES_S32 SAMPLE_COMM_ReadYUV(VIDEO_FRAME_INFO_S *pFrame, FILE *pFile, ES_U32 align, ES_U32 alignH);
ES_S32 SAMPLE_COMMON_SAVE_FRAME(VIDEO_FRAME_INFO_S *pFrame, uint8_t *buffer, ES_U32 align, ES_U32 alignH);

ES_S32 SAMPLE_COMM_INIT_BUFFER(VIDEO_FRAME_INFO_S *pFrame, SIZE_S size, PIXEL_FORMAT_E fmt, uint16_t devId);
ES_S32 SAMPLE_COMM_DEINIT_BUFFER(VIDEO_FRAME_INFO_S *pFrame);
__inline static const ES_CHAR *SAMPLE_COMMON_GetVbName(DIE_IDX nId)
{
    if (nId == ES_DIE_0) {
        return "mmz_nid_0_part_0";
    } else if (nId == ES_DIE_1) {
        return "mmz_nid_1_part_0";
    } else {
        return ES_NULL;
    }
}
#endif /* End of #ifndef __SAMPLE_COMMON_H__ */
