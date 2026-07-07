// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _MEMROYPOOL_H
#define _MEMROYPOOL_H

#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <stdexcept>
#include "sample_comm.h"

#define MAX_WIDTH 7680
#define MAX_HEIGHT 4320

class MemoryPool
{
 public:
    MemoryPool(size_t poolSize, uint16_t devId)
    {
        SIZE_S frameSize;
        frameSize.width = MAX_WIDTH;
        frameSize.height = MAX_HEIGHT;
        PIXEL_FORMAT_E fmt = PIXEL_FORMAT_R8G8B8;

        for (size_t i = 0; i < poolSize; ++i) {
            VIDEO_FRAME_INFO_S frame;
            memset(&frame, 0, sizeof(VIDEO_FRAME_INFO_S));
            ES_S32 ret = SAMPLE_COMM_INIT_BUFFER(&frame, frameSize, fmt, devId);
            if (ret < 0) {
                printf("init dstFrame buffer failed, ret: %d\n", ret);
                exit(0);
            }

            memoryQueue.push(frame);
        }
    }

    ~MemoryPool()
    {
        while (!memoryQueue.empty()) {
            VIDEO_FRAME_INFO_S frame;
            frame = memoryQueue.front();
            SAMPLE_COMM_DEINIT_BUFFER(&frame);
            memoryQueue.pop();
        }
    }

    bool allocate(VIDEO_FRAME_INFO_S &frame)
    {
        std::lock_guard<std::mutex> lock(mMemoryMutex);
        if (memoryQueue.empty()) {
            throw std::runtime_error("No available memory blocks");
        }

        VIDEO_FRAME_INFO_S vframe = memoryQueue.front();
        frame = vframe;
        memoryQueue.pop();

        return true;
    }

    void deallocate(VIDEO_FRAME_INFO_S &frame)
    {
        std::lock_guard<std::mutex> lock(mMemoryMutex);
        memoryQueue.push(frame);
    }

 private:
    std::queue<VIDEO_FRAME_INFO_S> memoryQueue;
    std::mutex mMemoryMutex;
};

#endif  // _MEMROYPOOL_H