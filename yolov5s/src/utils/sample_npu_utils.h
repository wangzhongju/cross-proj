// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef NPU_TEST_UTILS_H
#define NPU_TEST_UTILS_H
#include <vector>
#include <string>
#include "IPreprocess.h"
#include "es_nn_common.h"

static const uint32_t PROGRESS_BAR_WIDTH = 100;

std::vector<std::string> listSubDirectory(std::string dir);
int32_t extractModelFiles(std::vector<std::string> &modelDirs, std::vector<std::string> &modelFiles);
std::string extractConfigFiles(const std::string &configDir, const std::string &pattern);
std::vector<std::string> split(std::string input, char delimiter);
int32_t read_jpeg_data(const std::string &jpegPath, std::vector<char> &inFileData);
void showProgressBar(uint32_t progress, uint32_t progressWidth);
bool parsePreProcessParamsFromJson(const std::string &cfgFilePath, PREPROCERSS_PARAMS &params);
int32_t dump2File(uint64_t fd, uint64_t offset, uint64_t size, const std::string &dumpFilePath);
ES_DATA_PRECISION_E convertDataType(uint8_t dataType);

#endif  // NPU_TEST_UTILS_H