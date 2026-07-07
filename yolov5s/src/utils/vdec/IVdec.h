// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _IVDEC_H
#define _IVDEC_H

#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <stdexcept>
#include "sample_comm.h"

typedef struct {
    uint8_t *data;
    uint32_t len;
    std::string name;
} JPEG_DATA;

/**
 * @brief Structure representing a video frame with additional metadata.
 * VIDEO_FRAME_INFO_S is defined in /usr/include/essdk/es_comm_video.h
 
typedef struct esVIDEO_FRAME_S {
    ES_U32 width;
    ES_U32 height;
    VIDEO_FIELD_E field;
    PIXEL_FORMAT_E pixelFormat;
    DYNAMIC_RANGE_E dynamicRange;
    COLOR_GAMUT_E colorGamut;
    ES_U32 stride[3];
    ES_U32 offset[3];
    ES_U64 fd;
    ES_U32 maxLuminance;
    ES_U32 minLuminance;
    ES_U64 PTS;
    ES_U64 privateData;
    VIDEO_SUPPLEMENT_S supplement;
} VIDEO_FRAME_S;

// ! VB_POOL: /usr/include/essdk/es_common.h:47:typedef ES_U32 VB_POOL;
// ! MOD_ID_E: /usr/include/essdk/es_common.h:62
typedef enum esMOD_ID_E {
    ES_ID_VB = 1,
    ES_ID_SYS = 2,
    ES_ID_VDEC = 3,
    ES_ID_VPS = 4,
    ES_ID_VENC = 5,
    ES_ID_VO = 6,
    ES_ID_VI = 7,
    ES_ID_AIO = 8,
    ES_ID_AI = 9,
    ES_ID_AO = 10,
    ES_ID_AENC = 11,
    ES_ID_ADEC = 12,
    ES_ID_USER = 13,
    ES_ID_GDC = 14,
    ES_ID_NPU = 15,
    ES_ID_DSP = 16,
    ES_ID_BMS = 17,
    ES_ID_NUMA = 18,
    ES_ID_CIPHER = 19,
    ES_ID_AK = 20,
    ES_ID_MEMCP = 21,
    ES_ID_BUTT,
} MOD_ID_E;
 
typedef struct esVIDEO_FRAME_INFO_S {
    VIDEO_FRAME_S videoFrame;
    VB_POOL poolId;
    MOD_ID_E modId;
} VIDEO_FRAME_INFO_S;


 */
typedef struct {
    VIDEO_FRAME_INFO_S videoInfoFrame;
    std::string name;
} VDEC_VIDEO_FRAME;

class IVdec
{
 public:
    IVdec() {}
    virtual ~IVdec() {}

 public:
    /**
     * @brief Initialize the video decoder with the specified device ID.
     *
     * @return Returns 0 if initialization is successful, a negative error code otherwise.
     */
    virtual int32_t initVdec(int32_t dienum) = 0;
    /**
     * @brief Decode a JPEG image into a video frame.
     *
     * @param jpg: The JPEG data to be decoded.
     * @return Returns 0 if decoding is successful, a negative error code otherwise.
     */
    virtual int32_t decodeJpeg(const JPEG_DATA &jpg) = 0;
    /**
     * @brief Retrieve the next video frame from the decoder.
     *
     * @param frame: The decoded video frame to be filled.
     * @return Returns true if a video frame is successfully retrieved, false otherwise.
     */
    virtual bool getVideoFrame(VDEC_VIDEO_FRAME &frame) = 0;
    /**
     * @brief Release the memory or resources used by the video frame.
     *
     * @param frame: The video frame to be released.
     * @return None.
     */
    virtual void releaseFrame(VDEC_VIDEO_FRAME &frame) = 0;
    /**
     * @brief Flush the video decoder, clearing any buffered data.
     *
     * @return None.
     */
    virtual void flush() = 0;
    /**
     * @brief Deinitialize the video decoder and release associated resources.
     *
     * @return None.
     */
    virtual void deInitVdec() = 0;
};

#endif  // _IVDEC_H