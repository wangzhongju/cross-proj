// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _ESVDEC_H
#define _ESVDEC_H

#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <stdexcept>
#include "IVdec.h"
#include "MemoryPool.h"
#include "sample_comm.h"

class EsVdec : public IVdec
{
 public:
    explicit EsVdec(uint16_t devId);
    ~EsVdec();

 public:
    /**
     * @brief Initialize the video decoder with the specified device ID.
     *
     * @return Returns 0 if initialization is successful, a negative error code otherwise.
     */
    int32_t initVdec(int32_t dienum) override;
    /**
     * @brief Decode a JPEG image into a video frame.
     *
     * @param jpg: The JPEG data to be decoded.
     * @return Returns 0 if decoding is successful, a negative error code otherwise.
     */
    int32_t decodeJpeg(const JPEG_DATA &jpg) override;
    /**
     * @brief Retrieve the next video frame from the decoder.
     *
     * @param frame: The decoded video frame to be filled.
     * @return Returns true if a video frame is successfully retrieved, false otherwise.
     */
    bool getVideoFrame(VDEC_VIDEO_FRAME &frame) override;
    /**
     * @brief Release the memory or resources used by the video frame.
     *
     * @param frame: The video frame to be released.
     * @return None.
     */
    void releaseFrame(VDEC_VIDEO_FRAME &frame) override;
    /**
     * @brief Flush the video decoder, clearing any buffered data.
     *
     * @return None.
     */
    void flush() override;
    /**
     * @brief Deinitialize the video decoder and release associated resources.
     *
     * @return None.
     */
    void deInitVdec() override;

 private:
    ES_S32 mGrpCount;
    ES_S32 mGrpId;
    ES_S32 mChnId;

    bool mRunning;
    int32_t mBufferCapacity;
    uint16_t mDieId;
    mutable std::mutex mVideoMutex;
    std::condition_variable mVideoCond;
    std::queue<VDEC_VIDEO_FRAME> mVideoQueue;
    MemoryPool memoryPool;
    static std::atomic<bool> mInit;
    ES_S32 mCmdQHandle;
};

#endif  // _ESVDEC_H