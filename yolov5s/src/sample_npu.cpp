// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include "es_npu_interface.h"
#include "es_sys_memory.h"
#include "sample_npu_utils.h"
#include "EsVdec.h"
#include "EsHwPreProcess.h"
#include "EsPostProcess.h"
// #include "sample_npu_comm.h"
#include "sample_npu.h"



std::vector<std::vector<std::string>> parseInputFiles(std::string &inputDir)
{
    std::vector<std::vector<std::string>> inputFiles;

    std::vector<std::string> inputVec = listSubDirectory(inputDir);
    for (auto &input : inputVec) {
        inputFiles.push_back(listSubDirectory(input));
    }

    return inputFiles;
}

ES_VOID printUsage()
{
    printf("Usage:\n");
    printf("-h, --help                display help info\n");
    printf("-s, --sample=type         set sample type(\n"
           "                          1.SAMPLE_SYNC\n"
           "                          2.SAMPLE_ASYNC\n"
           "                          3.SAMPLE_MULTI_CONTEXT\n"
           "                          4.SAMPLE_MULTI_STREAM\n"
           "                          5.SAMPLE_MULTI_MODEL\n"
           "                          6.SAMPLE_COMPOSITE_MODEL\n"
           "                          7.SAMPLE_D2D\n"
           "                          8.SAMPLE_PEPELINE)\n");
    printf("-m, --model=dir           set model dirs.(Multiple model dirs are separated by commas)\n");
    printf("-i, --input=dir           set input dirs.(Multiple input dirs are separated by commas)\n");
    printf("-o, --output=dir          set output dirs.(Multiple input dirs are separated by commas)\n");
    printf("-p, --pre_process=path    set pre-process config file.(Multiple config file are separated by commas)\n");
    printf("-q, --post_process=path   set post-process config file.(Multiple config file are separated by commas)\n");
    printf("-t, --classify=path       set classify file.(Multiple classify file are separated by commas)\n");
    printf("-n, --numbers=n           set numbers of context/stream.\n");
}

ES_U16 deviceNum = 0;
void signalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        SAMPLE_COMM_VPS_Deinit();
        SAMPLE_COMM_VDEC_Stop(deviceNum);
        SAMPLE_COMM_VDEC_ExitVBPool();
        SAMPLE_COMM_VDEC_Deinit();
        ES_SYS_Exit();
        std::cout << "\nsample_npu exited!" << std::endl;
        exit(signal);
    }
}


int main(int argc, char **argv)
{
    // Register the signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    ES_U16 deviceId = 0;
    ES_S32 numbers = DEFAULT_NUMBERS;
    ES_S32 sampleType = 0;
    ES_S32 ret;
    std::vector<std::string> preConfigFileVec;
    std::vector<std::string> postConfigFileVec;
    std::vector<std::string> classifyFileVec;
    std::vector<std::string> modelDirVec;
    std::vector<std::string> inputDirVec;
    std::vector<std::string> outputDirVec;
    std::map<ES_U16, std::map<ES_U16, std::shared_ptr<IVdec>>> esVdecMap;
    std::map<ES_U16, std::map<ES_U16, std::shared_ptr<IPreprocess>>> preProcessMap;
    std::map<ES_U16, std::map<ES_U16, std::shared_ptr<EsPostProcess>>> postProcessMap;
    std::vector<std::vector<std::string>> inputs;
    std::vector<std::string> modelPaths;
    ES_S32 c = 0;
    ES_S32 option_index = 0;

    while (1) {
        c = getopt_long(argc, argv, "hs:m:i:o:p:q:t:n:d:", long_option, &option_index);
        if (c == -1) break;
        switch (c) {
            case 'h':
                printUsage();
                return 0;
            case 's':
                sampleType = atoi(optarg);
                break;
            case 'm':
                modelDirVec = split(std::string(optarg), ',');
                break;
            case 'i':
                inputDirVec = split(std::string(optarg), ',');
                break;
            case 'o':
                outputDirVec = split(std::string(optarg), ',');
                break;
            case 'p':
                preConfigFileVec = split(std::string(optarg), ',');
                break;
            case 'q':
                postConfigFileVec = split(std::string(optarg), ',');
                break;
            case 't':
                classifyFileVec = split(std::string(optarg), ',');
                break;
            case 'n':
                numbers = atoi(optarg);
                break;
            case 'd':
                deviceId = atoi(optarg);
                break;
            default:
                printf("arg parse error, option is invalid: %c\n", c);
                printUsage();
                return 0;
        }
    }

    if (optind < argc) {
        printf("arg parse error, optind: %d\n", optind);
        printUsage();
        return 0;
    }

    if (inputDirVec.empty()) {
        inputDirVec.push_back(DEFAULT_INPUT_DIR);
    }

    if (modelDirVec.empty()) {
        modelDirVec.push_back(DEFAULT_MODEL_DIR);
    }

    if (preConfigFileVec.empty()) {
        for (auto &entry : modelDirVec) {
            std::string preConfig;
            preConfig = extractConfigFiles(entry, "preprocess.json");
            if (preConfig.empty()) {
                printf("Can not find preprocess config file in %s\n", entry.c_str());
                return 0;
            }
            preConfigFileVec.push_back(preConfig);
        }
    }

    if (postConfigFileVec.empty()) {
        for (auto &entry : modelDirVec) {
            std::string postConfig;
            postConfig = extractConfigFiles(entry, "postprocess.json");
            if (postConfig.empty()) {
                printf("Can not find postprocess config file in %s\n", entry.c_str());
                return 0;
            }
            postConfigFileVec.push_back(postConfig);
        }
    }

    if (classifyFileVec.empty()) {
        for (auto &entry : modelDirVec) {
            std::string classesConfig;
            classesConfig = extractConfigFiles(entry, "classes.txt");
            classifyFileVec.push_back(classesConfig);
        }
    }

    if (modelDirVec.size() != inputDirVec.size() || modelDirVec.size() != preConfigFileVec.size() ||
        modelDirVec.size() != postConfigFileVec.size() || modelDirVec.size() != classifyFileVec.size()) {
        printf("Numbers of models is not equal to numbers of config or numbers of input\n");
        return 0;
    }

    ret = ES_NPU_GetNumDevices(&deviceNum);
    if (ret != ES_SUCCESS) {
        printf("ES_NPU_GetNumDevices failed\n");
        return 0;
    }

    for (ES_S32 modelIdx = 0; modelIdx < modelDirVec.size(); modelIdx++) {
        // esVdecVec[modelIdx] = {};
        // preProcessVec[modelIdx] = {};
        // postProcessVec[modelIdx] = {};
        for (ES_U16 dev = 0; dev < deviceNum; dev++) {
            esVdecMap[modelIdx].insert({dev, std::make_shared<EsVdec>(dev)});
            preProcessMap[modelIdx].insert({dev, std::make_shared<EsHwPreProcess>(dev)});
            postProcessMap[modelIdx].insert({dev, std::make_shared<EsPostProcess>(dev)});
        }
    }

    for (ES_S32 modelIdx = 0; modelIdx < modelDirVec.size(); modelIdx++) {
        for (ES_U16 dev = 0; dev < deviceNum; dev++) {
            if (esVdecMap[modelIdx][dev]->initVdec(deviceNum) != ES_SUCCESS) {
                printf("Error: Init video decoder failed.\n");
                return ES_FAILURE;
            }

            if (!preProcessMap[modelIdx][dev]->initializeFromJson(deviceId, preConfigFileVec[modelIdx])) {
                printf("PreProcess initializeFromJson failed, idx: %d\n", modelIdx);
                return 0;
            }

            if (!postProcessMap[modelIdx][dev]->parseArgs(postConfigFileVec[modelIdx])) {
                printf("PostProcess parseArgs failed, idx: %d\n", modelIdx);
                return 0;
            }

            PREPROCERSS_PARAMS &preProcessParams = preProcessMap[modelIdx][dev]->getParams();
            // Set the post-processing keep ratio from pre-processing
            POSTPROCESS_PARAMS postProcessParams;
            postProcessParams.keepRatio = preProcessParams.keepRatio;
            postProcessParams.keepLongSide = preProcessParams.keepLongSide;
            postProcessParams.paddingPosition = preProcessParams.paddingPosition;
            postProcessMap[modelIdx][dev]->setParams(postProcessParams);

            if (postProcessMap[modelIdx][dev]->getProcessType() == EsPostProcess::CLASSIFY ||
                postProcessMap[modelIdx][dev]->getProcessType() == EsPostProcess::DETECTION_OUT) {
                if (!postProcessMap[modelIdx][dev]->parseClassification(classifyFileVec[modelIdx])) {
                    printf("PostProcess parse classification text failed, idx: %d\n", modelIdx);
                    return 0;
                }
            }
        }
    }

    ret = extractModelFiles(modelDirVec, modelPaths);
    if (ret != ES_SUCCESS) {
        printf("extractModelFiles failed\n");
        return 0;
    }

    Config config;
    config.camera_index = "rtsp://admin:qazwsx12@@192.168.88.88:554/Streaming/Channels/1"; // Default camera index
    config.target_fps = 25; // Default target FPS
    config.enable_display = false; // Default to enable display
    config.enable_save = false; // Default to not save results
    config.use_direct_memory = false; // Default to use direct memory transfer
    config.dynamic_resolution = false; // Default to dynamic resolution adjustment

    if (sampleType == SAMPLE_SYNC) {
        ret = SAMPLE_NPU_COMM_InitDevice(deviceId);
        if (ret != ES_SUCCESS) {
            printf("SAMPLE_NPU_COMM_InitDevice failed, ret: 0x%x\n", ret);
            return 0;
        }
        ES_U32 modelId;
        ret = ES_NPU_LoadModelFromFile(&modelId, modelPaths[0].c_str());
        if (ret != ES_SUCCESS) {
            printf("Load model failed, ret: 0x%x\n", ret);
            goto releaseDevice;
            // return 0;
        }

        VideoProcessor processor(config,
                                 deviceId,
                                 modelId,
                                 esVdecMap[0][deviceId],
                                 preProcessMap[0][deviceId],
                                 postProcessMap[0][deviceId]);
        processor.start();

        std::cout << "Press 'q' to exit..." << std::endl;
        while (!processor.isStopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ES_NPU_UnloadModel(modelId);
    }
releaseDevice:
        SAMPLE_NPU_COMM_ReleaseDevice(deviceId);

    return 0;
}