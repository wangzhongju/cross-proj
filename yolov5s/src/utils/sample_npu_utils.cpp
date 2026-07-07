// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <filesystem>
#include <algorithm>
#include <sstream>
#include <fstream>
#include "sample_npu_utils.h"
#include "es_npu_types.h"
#include "cJSON.h"

#define GRAY_PADDING_VALUE 114

std::vector<std::string> listSubDirectory(std::string dir)
{
    std::vector<std::string> result;

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        result.push_back(entry.path().string());
    }

    std::sort(result.begin(), result.end());

    return result;
}

int32_t extractModelFiles(std::vector<std::string> &modelDirs, std::vector<std::string> &modelFiles)
{
    for (auto &dir : modelDirs) {
        std::string filePath;
        std::string fileName;
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".model") {
                filePath = entry.path().string();
                fileName = entry.path().filename().string();
                if (fileName.find("latency") != std::string::npos) break;
            }
        }
        if (!filePath.empty()) {
            modelFiles.push_back(filePath);
        } else {
            printf("No model file found in %s\n", dir.c_str());
            return -1;
        }
    }

    return 0;
}

/*
 * Extracts the first file matching the pattern from the specified directory.
 * Returns the file path if found, otherwise returns an empty string.
 */
std::string extractConfigFiles(const std::string &configDir, const std::string &pattern)
{
    std::string filePath;

    for (const auto &entry : std::filesystem::directory_iterator(configDir)) {
        if (entry.is_regular_file() && entry.path().filename().string().find(pattern) != std::string::npos) {
            filePath = entry.path().string();
            break;
        }
    }

    return filePath;
}

std::vector<std::string> split(std::string input, char delimiter)
{
    std::vector<std::string> result;
    std::string item;
    std::stringstream ss(input);

    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }

    return result;
}

int32_t read_jpeg_data(const std::string &jpegPath, std::vector<char> &inFileData)
{
    int32_t ret;
    std::ifstream inFileStream(jpegPath.c_str(), std::ios::binary | std::ios::ate);
    if (!inFileStream.is_open()) {
        printf("Failed to open file: %s\n", jpegPath.c_str());
        return -1;
    }

    std::streampos inFileLen = inFileStream.tellg();
    inFileStream.seekg(0, std::ios::beg);

    inFileData.resize(static_cast<size_t>(inFileLen));

    if (!inFileStream.read(inFileData.data(), inFileLen)) {
        printf("Failed to read file: %s\n", jpegPath.c_str());
        return -1;
    }

    return 0;
}

void showProgressBar(uint32_t progress, uint32_t progressWidth)
{
    std::string progressStr;

    progressStr.push_back('[');
    for (uint32_t i = 0; i < progressWidth; i++) {
        if (i < progress) {
            progressStr.push_back('=');
        } else if (i == progress) {
            progressStr.push_back('>');
        } else {
            progressStr.push_back(' ');
        }
    }
    progressStr += ("] " + std::to_string(progress * 100 / progressWidth) + "%");
    printf("%s\r", progressStr.c_str());
    fflush(stdout);
}

bool parsePreProcessParamsFromJson(const std::string &cfgFilePath, PREPROCERSS_PARAMS &params)
{
    std::ifstream file(cfgFilePath);
    if (!file.is_open()) {
        printf("Failed to open JSON file\n");
        return false;
    }

    std::string jsonString((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    cJSON *root = cJSON_Parse(jsonString.c_str());
    if (!root) {
        printf("Failed to parse JSON\n");
        return false;
    }

    cJSON *meanItem = cJSON_GetObjectItem(root, "mean");
    cJSON *stdItem = cJSON_GetObjectItem(root, "std");

    if (!meanItem || !stdItem) {
        printf("Failed to get mean and std\n");
        cJSON_Delete(root);
        return false;
    }

    if (cJSON_GetArraySize(meanItem) == 3) {
        params.meanValues[0] = cJSON_GetArrayItem(meanItem, 0)->valuedouble;
        params.meanValues[1] = cJSON_GetArrayItem(meanItem, 1)->valuedouble;
        params.meanValues[2] = cJSON_GetArrayItem(meanItem, 2)->valuedouble;
    } else {
        params.meanValues[0] = 123.675;
        params.meanValues[1] = 116.28;
        params.meanValues[2] = 103.53;
    }

    if (cJSON_GetArraySize(stdItem) == 3) {
        params.stdValues[0] = cJSON_GetArrayItem(stdItem, 0)->valuedouble;
        params.stdValues[1] = cJSON_GetArrayItem(stdItem, 1)->valuedouble;
        params.stdValues[2] = cJSON_GetArrayItem(stdItem, 2)->valuedouble;
    } else {
        params.stdValues[0] = 0.01712475;
        params.stdValues[1] = 0.017507;
        params.stdValues[2] = 0.01742919;
    }

    params.inputFormat = cJSON_GetObjectItem(root, "input_format")->valuestring;
    params.channelFirst = cJSON_GetObjectItem(root, "channel_first")->valueint;
    params.norm = cJSON_GetObjectItem(root, "norm")->valueint;
    params.cropHeight = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "crop_shape"), 0)->valueint;
    params.cropWidth = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "crop_shape"), 1)->valueint;
    params.quantizationScale = cJSON_GetObjectItem(root, "scale")->valuedouble;

    // resize params
    params.keepRatio = cJSON_GetObjectItem(root, "keep_ratio")->valueint;
    params.resizeShapeHeight = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "resize_shape"), 0)->valueint;
    if (params.keepRatio) {
        params.resizeShapeWidth = params.resizeShapeHeight;
    } else {
        params.resizeShapeWidth = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "resize_shape"), 1)->valueint;
    }
    if (cJSON_GetObjectItem(root, "keep_long_side") == NULL) {
        params.keepLongSide = true;
    } else {
        params.keepLongSide = cJSON_GetObjectItem(root, "keep_long_side")->valueint;
    }
    if (cJSON_GetObjectItem(root, "padding_data") == NULL) {
        params.paddingData = GRAY_PADDING_VALUE;
    } else {
        params.paddingData = cJSON_GetObjectItem(root, "padding_data")->valueint;
    }
    if (cJSON_GetObjectItem(root, "padding_position") == NULL) {
        params.paddingPosition = "Half";
    } else {
        params.paddingPosition = cJSON_GetObjectItem(root, "padding_position")->valuestring;
    }
    // printf("===============================================\n");
    // printf("Parsed Preprocess Configuration From JSON:\n");
    // printf("mean: [%f, %f, %f]\n", params.meanValues[0], params.meanValues[1], params.meanValues[2]);
    // printf("std: [%f, %f, %f]\n", params.stdValues[0], params.stdValues[1], params.stdValues[2]);
    // printf("input_format: %s\n", params.inputFormat.c_str());
    // printf("keep_ratio: %s\n", params.keepRatio ? "true" : "false");
    // printf("channel_first: %s\n", params.channelFirst ? "true" : "false");
    // printf("norm: %s\n", params.norm ? "true" : "false");
    // printf("resize_shape_w: %d\n", params.resizeShapeWidth);
    // printf("resize_shape_h: %d\n", params.resizeShapeHeight);
    // printf("crop_shape: [%d, %d]\n", params.cropWidth, params.cropHeight);
    // printf("quantization_scale: %f\n", params.quantizationScale);
    // printf("===============================================\n");

    cJSON_Delete(root);

    return true;
}

int32_t dump2File(uint64_t fd, uint64_t offset, uint64_t size, const std::string &dumpFilePath)
{
    uint8_t *pOutData = (uint8_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
    if (!pOutData) {
        printf("ES_SYS_Mmap failed!\n");
        return ES_FAILURE;
    }

    std::ofstream outFile(dumpFilePath, std::ios::binary);
    outFile.write(reinterpret_cast<const char *>(pOutData + offset), size);
    if (!outFile) {
        printf("Failed to dump preprocess data to file: %s\n", dumpFilePath.c_str());
        ES_SYS_Munmap(pOutData, size + offset);
        return ES_FAILURE;
    }

    outFile.close();
    ES_SYS_Munmap(pOutData, size + offset);

    return ES_SUCCESS;
}

ES_DATA_PRECISION_E convertDataType(uint8_t dataType)
{
    switch (dataType) {
        case ES_NPU_DATA_TYPE_FLOAT:
            return ES_PRECISION_FP32;
        case ES_NPU_DATA_TYPE_HALF:
            return ES_PRECISION_FP16;
        case ES_NPU_DATA_TYPE_INT16:
            return ES_PRECISION_INT16;
        case ES_NPU_DATA_TYPE_INT8:
            return ES_PRECISION_INT8;
        case ES_NPU_DATA_TYPE_UINT8:
            return ES_PRECISION_UINT8;
        case ES_NPU_DATA_TYPE_UINT16:
            return ES_PRECISION_UINT16;
        case ES_NPU_DATA_TYPE_INT32:
            return ES_PRECISION_INT32;
        case ES_NPU_DATA_TYPE_UINT32:
            return ES_PRECISION_UINT32;
        case ES_NPU_DATA_TYPE_INT64:
            return ES_PRECISION_INT64;
        case ES_NPU_DATA_TYPE_UINT64:
            return ES_PRECISION_UINT64;
        case ES_NPU_DATA_TYPE_UNKNOWN:
        default:
            printf("data type:%d not support\n", dataType);
            return ES_PRECISION_UNKNOWN;
    }
}