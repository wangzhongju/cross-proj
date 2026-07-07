// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _IPreprocess_H
#define _IPreprocess_H

#include <stdint.h>
#include <string>
#include "sample_comm.h"

typedef struct {
    uint64_t fd;
    uint64_t offset;
    uint64_t size;
} PREPROCESSED_DATA;

typedef struct {
    double meanValues[3];
    double stdValues[3];
    int resizeShapeWidth;
    int resizeShapeHeight;
    int cropWidth;
    int cropHeight;
    double quantizationScale;
    bool channelFirst;
    bool norm;
    bool keepRatio;
    bool keepLongSide;
    int paddingData;
    std::string paddingPosition;
    std::string inputFormat;
} PREPROCERSS_PARAMS;

class IPreprocess
{
 public:
    IPreprocess() {}
    virtual ~IPreprocess() {}

 public:
    /**
     * @brief Initialize from a JSON configuration file.
     *
     * @param devId: Device ID for initialization.
     * @param cfgFilePath: Path to the JSON configuration file.
     * @return Returns true if initialization is successful, false otherwise.
     */
    virtual bool initializeFromJson(uint32_t devId, const std::string &cfgFilePath) = 0;

    /**
     * @brief Perform preprocessing on the input video frame.
     *
     * @param frame: The input video frame data.
     * @param outData: The output data after preprocessing.
     * @return Returns an error code, 0 if successful.
     */
    virtual int32_t preprocess(VIDEO_FRAME_INFO_S &frame, PREPROCESSED_DATA &outData) = 0;

    /**
     * @brief Return the reference to the preprocess parameters..
     *
     * @return A reference to the preprocess parameters (PREPROCESS_PARAMS).
     */
    virtual PREPROCERSS_PARAMS &getParams() { return mPreprocessParams; }

    /**
     * @brief Release any resources or memory used during processing.
     *
     * @return None.
     */
    virtual void release() = 0;

 protected:
    PREPROCERSS_PARAMS mPreprocessParams;
};

#endif  // _IPreprocess_H
