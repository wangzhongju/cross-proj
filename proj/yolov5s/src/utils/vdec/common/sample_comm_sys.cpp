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
#include <sys/mman.h>
#include "sample_comm.h"

typedef struct esSAMPLE_PERF_S {
    ES_U64 inTick;
    ES_U64 outTick;
    ES_U32 frmCnt;
} SAMPLE_PERF_S;

static SAMPLE_PERF_S gSamplePerf = {0};
static pthread_mutex_t gInMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gOutMutex = PTHREAD_MUTEX_INITIALIZER;

#define FILE_WRITE_RETRY_TIME (50)

/******************************************************************************
 * function : vb init & system init
 ******************************************************************************/
ES_S32 SAMPLE_COMM_SYS_Init(VB_CONFIG_S *pVbConfig) {
    ES_S32 ret = ES_FAILURE;

    SAMPLE_COMM_SYS_Exit();

    ret = ES_VB_SetConfig(pVbConfig);
    if (ES_SUCCESS != ret) {
        SAMPLE_PRT("ES_VB_SetConf failed!\n");
        return ES_FAILURE;
    }

    ret = ES_VB_Init();
    if (ES_SUCCESS != ret) {
        SAMPLE_PRT("ES_VB_Init failed!\n");
        return ES_FAILURE;
    }

    ret = ES_SYS_Init();
    if (ES_SUCCESS != ret) {
        SAMPLE_PRT("ES_SYS_Init failed!\n");
        return ES_FAILURE;
    }

    return ES_SUCCESS;
}

/******************************************************************************
 * function : vb exit & system exit
 ******************************************************************************/
ES_VOID SAMPLE_COMM_SYS_Exit(ES_VOID) {
    ES_SYS_Exit();
    VB_CONFIG_S config;
    memset(&config, 0, sizeof(VB_CONFIG_S));
    ES_VB_GetModPoolConfig(VB_UID_VDEC, &config);
    if (config.poolCnt) {
        SAMPLE_PRT("Exit vdec mod pool!\n");
        ES_VB_ModPoolExit(VB_UID_VDEC);
    }

    memset(&config, 0, sizeof(VB_CONFIG_S));
    ES_VB_GetConfig(&config);
    if (config.poolCnt) {
        SAMPLE_PRT("Exit vb!\n");
        ES_VB_Exit();
    }
}

static size_t fileWrite(ES_VOID *pAddr, size_t expectSize, FILE *pFile) {
    ES_U8 retryCnt = 0;
    size_t offset = 0;
    size_t returnSize = 0;
    size_t totalSize = 0;
    size_t writeSize = expectSize;
    do {
        returnSize = fwrite((uint8_t *)pAddr + offset, 1, writeSize, pFile);
        if (returnSize == 0) {
            SAMPLE_PRT("fwrite return error: %d'%s'\n", errno, strerror(errno));
            break;
        } else if (returnSize < writeSize) {
            offset += returnSize;
            writeSize -= returnSize;
            totalSize += returnSize;
            retryCnt++;
            if (retryCnt >= FILE_WRITE_RETRY_TIME) {
                SAMPLE_PRT("Expect write %zu, but write %zu after retry %d times, err=%d'%s'\n",
                           expectSize,
                           totalSize,
                           FILE_WRITE_RETRY_TIME,
                           errno,
                           strerror(errno));
                break;
            }
        } else {
            totalSize += returnSize;
            break;
        }
    } while (1);
    return totalSize;
}

static ES_U64 calcBufParams(VIDEO_FRAME_S *pFrame,
                            ES_U32 widthOut[3],
                            ES_U32 heightOut[3],
                            ES_U32 *pAlignWidth,
                            ES_U32 *pPlane,
                            ES_U32 offsetOut[3],
                            ES_S32 align,
                            ES_U32 alignH) {
    if (!pFrame) return 0;

    ES_U32 imageWidth = pFrame->width;
    ES_U32 imageHeight = pFrame->height;
    ES_U32 alignWidth = align > 0 ? ES_ALIGN_UP(imageWidth, align) : imageWidth;
    ES_U32 plane, stride[3] = {0}, width[3] = {0}, height[3] = {0}, offset[3] = {0};
    ES_U64 size;

    size =
        COMMON_GetPicBufInfo(pFrame->pixelFormat, pFrame->width, pFrame->height, align, alignH, stride, offset, &plane);
    if (!size) return 0;

    if (!pFrame->stride[0]) {
        pFrame->stride[0] = stride[0];
        pFrame->stride[1] = stride[1];
        pFrame->stride[2] = stride[2];
    }

    switch (pFrame->pixelFormat) {
        case PIXEL_FORMAT_NV12:
        case PIXEL_FORMAT_NV21:
            width[0] = imageWidth;
            height[0] = imageHeight;
            width[1] = imageWidth;
            height[1] = imageHeight / 2;
            break;
        case PIXEL_FORMAT_I420:
        case PIXEL_FORMAT_YV12:
            width[0] = imageWidth;
            height[0] = imageHeight;
            width[1] = width[2] = imageWidth / 2;
            height[1] = height[2] = imageHeight / 2;
            break;
        case PIXEL_FORMAT_YUV420SP010BE:
        case PIXEL_FORMAT_YVU420SP010BE:
        case PIXEL_FORMAT_YUV420SP010LE:
        case PIXEL_FORMAT_YVU420SP010LE:
            width[0] = imageWidth * 2;
            height[0] = imageHeight;
            width[1] = imageWidth * 2;
            height[1] = imageHeight / 2;
            break;
        case PIXEL_FORMAT_YUV420P010LE:
        case PIXEL_FORMAT_YUV420P010BE:
        case PIXEL_FORMAT_YVU420P010LE:
        case PIXEL_FORMAT_YVU420P010BE:
            width[0] = imageWidth * 2;
            height[0] = imageHeight;
            width[1] = width[2] = imageWidth;
            height[1] = height[2] = imageHeight / 2;
            break;
        case PIXEL_FORMAT_NV16:
        case PIXEL_FORMAT_NV61:
            width[0] = imageWidth;
            height[0] = imageHeight;
            width[1] = imageWidth;
            height[1] = imageHeight;
            break;
        case PIXEL_FORMAT_YUV422SP010BE:
        case PIXEL_FORMAT_YVU422SP010BE:
        case PIXEL_FORMAT_YUV422SP010LE:
        case PIXEL_FORMAT_YVU422SP010LE:
            width[0] = imageWidth * 2;
            height[0] = imageHeight;
            width[1] = imageWidth * 2;
            height[1] = imageHeight;
            break;
        default:
            break;
    }

    if (width[0] == 0) {
        for (ES_U32 i = 0; i < plane; i++) {
            ES_FLOAT factor = ((ES_U32)((2.0 * pFrame->stride[i] / alignWidth) + 0.5)) / 2.0;  // nearest to 0.5
            width[i] = imageWidth * factor;
            height[i] = imageHeight;
        }
    }

    size = 0;
    for (ES_U32 i = 0; i < plane; i++) {
        ES_U32 alignHeight = height[i];
        if (alignH > 0) {
            ES_U32 ratio = imageHeight / height[i];
            alignHeight = ES_ALIGN_UP(height[i], alignH / ratio);
        }
        size += pFrame->stride[i] * alignHeight;

        if (offsetOut) offsetOut[i] = offset[i];
        if (widthOut) widthOut[i] = width[i];
        if (heightOut) heightOut[i] = height[i];
    }

    if (pAlignWidth) {
        *pAlignWidth = alignWidth;
    }

    if (pPlane) {
        *pPlane = plane;
    }

    return size;
}

static ES_S32 saveOrLoadFrame(VIDEO_FRAME_INFO_S *pFrame, FILE *pFile, ES_U32 align, ES_U32 alignH, ES_BOOL bRead) {
    if (!pFrame || !pFrame->videoFrame.width || !pFrame->videoFrame.height) {
        return -1;
    }
    if (!bRead && !pFrame->videoFrame.fd) {
        return -1;
    }
    ES_S32 writeOrReadSize = 0;
    ES_U32 plane = 1;
    ES_U32 alignWidth;
    ES_U32 width[3] = {0};
    ES_U32 height[3] = {0};
    ES_U32 offset[3] = {0};
    ES_U64 bufferSize = 0;
    ES_VOID *pMapVirAddr = ES_NULL;
    if (!(bufferSize = calcBufParams(&pFrame->videoFrame, width, height, &alignWidth, &plane, offset, align, alignH))) {
        return -1;
    }
    if (bRead && !pFrame->videoFrame.fd) {
        ES_U64 dmaFd = 0;
        if (ES_SUCCESS != ES_VB_GetBlock(pFrame->poolId, bufferSize, ES_NULL, &dmaFd)) {
            SAMPLE_PRT("Read: Get block with %llu bytes from pool[%u] failed.\n", bufferSize, pFrame->poolId);
            return -1;
        }
        pFrame->videoFrame.fd = dmaFd;
        pFrame->videoFrame.offset[0] = offset[0];
        pFrame->videoFrame.offset[1] = offset[1];
        pFrame->videoFrame.offset[2] = offset[2];
    } else if (!bRead) {
        ES_BOOL bSetOffset = ES_FALSE;
        for (ES_U32 i = 0; i < plane; i++) {
            if (pFrame->videoFrame.offset[i] > 0) {
                bSetOffset = ES_TRUE;
            }
        }
        if (bSetOffset) {
            offset[0] = pFrame->videoFrame.offset[0];
            offset[1] = pFrame->videoFrame.offset[1];
            offset[2] = pFrame->videoFrame.offset[2];
        } else {
            offset[0] = 0;
            offset[1] = pFrame->videoFrame.stride[0] * height[0];
            offset[2] = pFrame->videoFrame.stride[1] * height[1] + offset[1];
        }
        bufferSize = offset[plane - 1] + pFrame->videoFrame.stride[plane - 1] * height[plane - 1];
    }

    if (!pFile) {
        return COMMON_GetBpp(pFrame->videoFrame.pixelFormat) * pFrame->videoFrame.width * pFrame->videoFrame.height / 8;
    }
    pMapVirAddr = ES_SYS_Mmap(pFrame->videoFrame.fd, bufferSize, SYS_CACHE_MODE_NOCACHE);
    for (ES_U32 i = 0; i < plane; i++) {
        ES_U32 plane_size = 0;
        ES_U8 *pVirAddr = (ES_U8 *)pMapVirAddr + offset[i];
        for (ES_U32 j = 0; j < height[i]; j++) {
            if (bRead) {
                plane_size += fread(pVirAddr + pFrame->videoFrame.stride[i] * j, 1, width[i], pFile);
            } else {
                ES_S32 writeSize = fileWrite(pVirAddr + pFrame->videoFrame.stride[i] * j, width[i], pFile);
                if (writeSize < 0) {
                    goto on_error;
                }
                plane_size += writeSize;
            }
        }
        // SAMPLE_PRT("plane%d: alignW=%d stride=%d %dx%d %s bytes=%d offset=%u frame offset=%u\n",
        //            i,
        //            alignWidth,
        //            pFrame->videoFrame.stride[i],
        //            width[i],
        //            height[i],
        //            (bRead) ? "read" : "write",
        //            plane_size,
        //            offset[i],
        //            pFrame->videoFrame.offset[i]);
        if (plane_size == 0) {
            break;
        }
        writeOrReadSize += plane_size;
    }
on_error:
    ES_SYS_Munmap(pMapVirAddr, bufferSize);
    if (!bRead) {
        fflush(pFile);
    }
    return writeOrReadSize;
}

ES_S32 SAMPLE_COMMON_SAVE_FRAME(VIDEO_FRAME_INFO_S *pFrame, uint8_t *buffer, ES_U32 align, ES_U32 alignH) {
    if (!pFrame || !pFrame->videoFrame.width || !pFrame->videoFrame.height) {
        return -1;
    }
    if (!pFrame->videoFrame.fd) {
        return -1;
    }

    ES_S32 writeOrReadSize = 0;
    ES_U32 plane = 1;
    ES_U32 alignWidth;
    ES_U32 width[3] = {0};
    ES_U32 height[3] = {0};
    ES_U32 offset[3] = {0};
    ES_U64 bufferSize = 0;
    ES_VOID *pMapVirAddr = ES_NULL;
    if (!(bufferSize = calcBufParams(&pFrame->videoFrame, width, height, &alignWidth, &plane, offset, align, alignH))) {
        return -1;
    }

    ES_BOOL bSetOffset = ES_FALSE;
    for (ES_U32 i = 0; i < plane; i++) {
        if (pFrame->videoFrame.offset[i] > 0) {
            bSetOffset = ES_TRUE;
        }
    }
    if (bSetOffset) {
        offset[0] = pFrame->videoFrame.offset[0];
        offset[1] = pFrame->videoFrame.offset[1];
        offset[2] = pFrame->videoFrame.offset[2];
    } else {
        offset[0] = 0;
        offset[1] = pFrame->videoFrame.stride[0] * height[0];
        offset[2] = pFrame->videoFrame.stride[1] * height[1] + offset[1];
    }
    bufferSize = offset[plane - 1] + pFrame->videoFrame.stride[plane - 1] * height[plane - 1];

    if (!buffer) {
        return COMMON_GetBpp(pFrame->videoFrame.pixelFormat) * pFrame->videoFrame.width * pFrame->videoFrame.height / 8;
    }

    pMapVirAddr = ES_SYS_Mmap(pFrame->videoFrame.fd, bufferSize, SYS_CACHE_MODE_NOCACHE);
    for (ES_U32 i = 0; i < plane; i++) {
        ES_U32 plane_size = 0;
        ES_U8 *pVirAddr = (ES_U8 *)pMapVirAddr + offset[i];
        for (ES_U32 j = 0; j < height[i]; j++) {
            memcpy(buffer, pVirAddr + pFrame->videoFrame.stride[i] * j, width[i]);
            ES_S32 writeSize = width[i];
            buffer += width[i];
            plane_size += writeSize;
        }

        if (plane_size == 0) {
            break;
        }
        writeOrReadSize += plane_size;
    }
on_error:
    ES_SYS_Munmap(pMapVirAddr, bufferSize);
    return writeOrReadSize;
}

ES_S32 SAMPLE_COMM_WriteYUV(VIDEO_FRAME_INFO_S *pFrame, FILE *pFile, ES_U32 align, ES_U32 alignH) {
    return saveOrLoadFrame(pFrame, pFile, align, alignH, ES_FALSE);
}

ES_S32 SAMPLE_COMM_ReadYUV(VIDEO_FRAME_INFO_S *pFrame, FILE *pFile, ES_U32 align, ES_U32 alignH) {
    return saveOrLoadFrame(pFrame, pFile, align, alignH, ES_TRUE);
}
