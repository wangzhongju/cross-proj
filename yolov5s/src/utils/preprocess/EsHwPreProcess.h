// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _HWPREPROCESS_H
#define _HWPREPROCESS_H

#include "IPreprocess.h"
#include <string>
#include <atomic>
#include <mutex>
#include "sample_comm.h"

class EsHwPreProcess : public IPreprocess
{
 public:
    explicit EsHwPreProcess(ES_U16 devId);
    virtual ~EsHwPreProcess();

 public:
    bool initializeFromJson(uint32_t devId, const std::string &cfgFilePath) override;
    int32_t preprocess(VIDEO_FRAME_INFO_S &frame, PREPROCESSED_DATA &outData) override;
    void release() override;

 private:
    ES_S32 mGrpCount;
    ES_S32 mGrpId;
    ES_S32 mChnId;
    ES_U16 mDieId;

    VPS_NORMALIZATION_PARAMS_S mNormParams;
    VIDEO_FRAME_INFO_S mMiddFrame;
    VIDEO_FRAME_INFO_S mDstFrame;
    bool mIs16ByteAligned;
    static std::atomic<bool> mInit;
    std::mutex mMtx;
};

#endif  // _PREPROCESS_H
