#ifndef SAMPLE_NPU_H
#define SAMPLE_NPU_H


#include <string>
#include <queue>
#include <fstream>
#include <numeric>  // std::accumulate
#include <mutex>
#include <memory>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/core/utils/filesystem.hpp>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <map>
#include <getopt.h>
#include <csignal>
#include "sample_npu_comm.h"



static const std::string DEFAULT_INPUT_DIR(
    "/opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/input/pictures/");
static const std::string DEFAULT_MODEL_DIR("/opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/");
static const ES_S32 DEFAULT_NUMBERS = 4;

enum {
    SAMPLE_SYNC = 1,
    SAMPLE_ASYNC,
    SAMPLE_MULTI_CONTEXT,
    SAMPLE_MULTI_STREAM,
    SAMPLE_MULTI_MODEL,
    SAMPLE_COMPOSITE_MODEL,
    SAMPLE_D2D,
    SAMPLE_PEPELINE,
};

static struct option long_option[] = {
    {"help", no_argument, NULL, 'h'},
    {"sample", required_argument, NULL, 's'},
    {"model", required_argument, NULL, 'm'},
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"pre_process", required_argument, NULL, 'p'},
    {"post_process", required_argument, NULL, 'q'},
    {"classes", required_argument, NULL, 't'},
    {"numbers", required_argument, NULL, 't'},
    {"device", required_argument, NULL, 'd'},
};

ES_S32 SAMPLE_NPU_Sync(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> &inputFiles,
                       const std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                       std::shared_ptr<IPreprocess> preProcess, std::shared_ptr<EsPostProcess> postProcess)
{
    return SAMPLE_NPU_COMM_SubmitSync(devId, modelId, inputFiles, outputDir, esVdec, preProcess, postProcess);
}

ES_S32 SAMPLE_NPU_ASync(ES_U16 devId, ES_U32 modelId, std::vector<std::vector<std::string>> &inputFiles,
                        std::string &outputDir, std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                        std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret = ES_SUCCESS, err = ES_SUCCESS;
    ES_U32 taskId;
    std::atomic<ES_S32> completeNum(0);
    SampleStateInfo sampleInfo;
    npu_stream stream;
    npu_context context;

    ret = ES_NPU_CreateContext(&context, devId);
    if (ret != ES_SUCCESS) {
        printf("ES_NPU_CreateContext failed, ret: 0x%x\n", ret);
        return ES_FAILURE;
    }

    ret = ES_NPU_CreateStream(&stream);
    if (ret != ES_SUCCESS) {
        printf("ES_NPU_CreateStream failed, ret: 0x%x\n", ret);
        goto destroy_context;
    }

    ret = SAMPLE_NPU_COMM_SubmitAsync(devId, modelId, stream, inputFiles, outputDir, esVdec, preProcess, postProcess);
    if (ret != ES_SUCCESS) {
        printf("SAMPLE_NPU_COMM_SubmitAsync failed\n");
    }

    err = ES_NPU_DestroyStream(stream);
    if (err != ES_SUCCESS) {
        printf("ES_NPU_DestroyStream failed, ret: 0x%x\n", ret);
    }

destroy_context:
    err = ES_NPU_DestroyContext(context);
    if (err != ES_SUCCESS) {
        printf("ES_NPU_DestroyContext failed, ret: 0x%x\n", ret);
    }

    return ret | err;
}

ES_S32 SAMPLE_NPU_MultiContext(ES_U16 devId, ES_S32 nums, ES_U32 modelId,
                               std::vector<std::vector<std::string>> &inputFiles, std::string &outputDir,
                               std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                               std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret;
    std::vector<npu_context> contextVec;
    ES_S32 contextId = 0;

    for (contextId = 0; contextId < nums; contextId++) {
        npu_context context;
        ret = ES_NPU_CreateContext(&context, devId);
        if (ret != ES_SUCCESS) {
            printf("ES_NPU_CreateContext, ret: 0x%x\n", ret);
            goto destroy_context;
        }
        contextVec.push_back(context);
    }

    for (auto &context : contextVec) {
        npu_stream stream;
        ret = ES_NPU_SetCurrentContext(context);
        if (ret != ES_SUCCESS) {
            printf("ES_NPU_CreateContext, ret: 0x%x\n", ret);
            goto destroy_context;
        }

        ret = ES_NPU_CreateStream(&stream);
        if (ret != ES_SUCCESS) {
            printf("ES_NPU_CreateStream, ret: 0x%x\n", ret);
            goto destroy_context;
        }

        ret =
            SAMPLE_NPU_COMM_SubmitAsync(devId, modelId, stream, inputFiles, outputDir, esVdec, preProcess, postProcess);
        if (ret != ES_SUCCESS) {
            printf("SAMPLE_NPU_COMM_SubmitAsync failed\n");
        }

        ret |= ES_NPU_DestroyStream(stream);
        if (ret != ES_SUCCESS) {
            goto destroy_context;
        }
    }

destroy_context:
    for (auto &context : contextVec) {
        ret |= ES_NPU_DestroyContext(context);
    }

    return ret;
}

ES_S32 SAMPLE_NPU_MultiStream(ES_U16 devId, ES_S32 nums, ES_U32 modelId,
                              std::vector<std::vector<std::string>> &inputFiles, std::string &outputDir,
                              std::shared_ptr<IVdec> esVdec, std::shared_ptr<IPreprocess> preProcess,
                              std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret;
    npu_context context;
    ES_S32 streamId = 0;
    std::vector<npu_stream> streamVec;

    ret = ES_NPU_CreateContext(&context, devId);
    if (ret != ES_SUCCESS) {
        printf("ES_NPU_CreateContext, ret: 0x%x\n", ret);
        return ret;
    }

    for (streamId = 0; streamId < nums; streamId++) {
        npu_stream stream;
        ret = ES_NPU_CreateStream(&stream);
        if (ret != ES_SUCCESS) {
            printf("ES_NPU_CreateStream, ret: 0x%x\n", ret);
            goto destroy_stream;
        }
        streamVec.push_back(stream);
    }

    for (auto &stream : streamVec) {
        ret =
            SAMPLE_NPU_COMM_SubmitAsync(devId, modelId, stream, inputFiles, outputDir, esVdec, preProcess, postProcess);
        if (ret != ES_SUCCESS) {
            printf("SAMPLE_NPU_COMM_SubmitAsync failed\n");
            goto destroy_stream;
        }
    }

destroy_stream:
    for (auto &stream : streamVec) {
        ret |= ES_NPU_DestroyStream(stream);
    }

    return ret;
}

ES_S32 SAMPLE_NPU_MultiModel(ES_U16 devId, std::vector<std::string> &modelPaths, std::vector<std::string> &inputsDir,
                             std::vector<std::string> &outputDirs,
                             std::map<ES_U16, std::map<ES_U16, std::shared_ptr<IVdec>>> &esVdec,
                             std::map<ES_U16, std::map<ES_U16, std::shared_ptr<IPreprocess>>> &preProcess,
                             std::map<ES_U16, std::map<ES_U16, std::shared_ptr<EsPostProcess>>> &postProcess)
{
    ES_S32 ret;
    ES_U32 modelId;
    std::vector<ES_U32> modelIdVec;
    std::vector<std::string> inputs;
    std::vector<std::vector<std::string>> inputFiles;

    for (auto &modelPath : modelPaths) {
        ret = ES_NPU_LoadModelFromFile(&modelId, modelPath.c_str());
        if (ret != ES_SUCCESS) {
            printf("ES_NPU_LoadModelFromFile failed, ret: 0x%x\n", ret);
            goto unload_model;
        }
        modelIdVec.push_back(modelId);
    }

    for (ES_S32 i = 0; i < modelIdVec.size(); i++) {
        std::string outputDir;
        inputFiles.clear();
        inputs = listSubDirectory(inputsDir[i]);
        for (auto &input : inputs) {
            inputFiles.push_back(listSubDirectory(input));
        }
        if (inputFiles.empty() || inputFiles.front().empty()) {
            printf("No input file found, inputDir: %s\n", inputsDir[i].c_str());
            ret = ES_FAILURE;
            goto unload_model;
        }

        if (outputDirs.size() > i) {
            outputDir = outputDirs[i];
        }

        ret = SAMPLE_NPU_COMM_SubmitSync(devId, modelIdVec[i], inputFiles, outputDir, esVdec[i][devId],
                                         preProcess[i][devId], postProcess[i][devId]);
        if (ret != ES_SUCCESS) {
            printf("SAMPLE_NPU_COMM_SubmitSync failed\n");
            goto unload_model;
        }
    }

unload_model:
    for (auto &id : modelIdVec) {
        ret |= ES_NPU_UnloadModel(id);
    }

    return ret;
}

ES_S32 SAMPLE_NPU_InitEnv(ES_U32 modelId, ES_U16 devId, npu_stream &stream, npu_context &context,
                          NPU_FLEXIBLE_TASK_ATTR_S &attr)
{
    ES_S32 ret = ES_NPU_CreateContext(&context, devId);
    if (ret != ES_SUCCESS) {
        printf("(%s, %d)ES_NPU_CreateContext failed, ret: 0x%x\n", __func__, __LINE__, ret);
        return ES_FAILURE;
    }

    attr.timeOut = 100;
    ret = ES_NPU_SetFlexibleTaskAttr(modelId, &attr);
    if (ret != ES_SUCCESS) {
        printf("(%s, %d)ES_NPU_SetFlexibleTaskAttr failed, ret: 0x%x\n", __func__, __LINE__, ret);
        ES_NPU_DestroyContext(context);
        return ret;
    }

    ret = ES_NPU_CreateStream(&stream);
    if (ret != ES_SUCCESS) {
        printf("(%s, %d)ES_NPU_CreateStream failed, ret: 0x%x\n", __func__, __LINE__, ret);
        ES_NPU_DestroyContext(context);
        return ret;
    }

    return ES_SUCCESS;
}

ES_S32 SAMPLE_NPU_DeinitEnv(npu_stream stream, npu_context context)
{
    ES_S32 err;
    err = ES_NPU_DestroyStream(stream);
    if (err != ES_SUCCESS) {
        printf("ES_NPU_DestroyStream failed, ret: 0x%x\n", err);
    }

    err = ES_NPU_DestroyContext(context);
    if (err != ES_SUCCESS) {
        printf("ES_NPU_DestroyContext failed, ret: 0x%x\n", err);
    }

    return err;
}

ES_S32 SAMPLE_NPU_CompositeModel(ES_U32 modelId, ES_U16 devId, std::vector<std::vector<std::string>> &inputFiles,
                                 std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                                 std::shared_ptr<IPreprocess> preProcess, std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret, err;
    npu_stream stream;
    npu_context context;
    NPU_FLEXIBLE_TASK_ATTR_S attr;

    ret = SAMPLE_NPU_InitEnv(modelId, devId, stream, context, attr);
    if (ret != ES_SUCCESS) {
        return ret;
    }

    ret = SAMPLE_NPU_COMMON_SubmitUserTask(modelId, stream, inputFiles, outputDir, esVdec, preProcess, postProcess);
    if (ret != ES_SUCCESS) {
        printf("(%s, %d)SAMPLE_NPU_COMMON_SubmitUserTask failed, ret: 0x%x\n", __func__, __LINE__, ret);
    }

    err = SAMPLE_NPU_DeinitEnv(stream, context);

    return err | ret;
}

ES_S32 SAMPLE_NPU_Pipeline(ES_U32 modelId, ES_U16 devId, std::vector<std::vector<std::string>> &inputFiles,
                           std::string &outputDir, std::shared_ptr<IVdec> esVdec,
                           std::shared_ptr<IPreprocess> preProcess, std::shared_ptr<EsPostProcess> postProcess)
{
    ES_S32 ret, err;
    npu_stream stream;
    npu_context context;
    NPU_FLEXIBLE_TASK_ATTR_S attr;

    ret = SAMPLE_NPU_InitEnv(modelId, devId, stream, context, attr);
    if (ret != ES_SUCCESS) {
        return ret;
    }

    ret = SAMPLE_NPU_COMMON_SubmitPipelineTask(modelId, stream, inputFiles, outputDir, esVdec, preProcess, postProcess);
    if (ret != ES_SUCCESS) {
        printf("(%s, %d)SAMPLE_NPU_COMMON_SubmitPipelineTask failed, ret: 0x%x\n", __func__, __LINE__, ret);
    }

    err = SAMPLE_NPU_DeinitEnv(stream, context);

    return err | ret;
}

ES_S32 SAMPLE_NPU_D2D(std::vector<std::vector<std::string>> &inputFiles, std::string &outputDir, std::string &modelPath,
                      std::map<ES_U16, std::shared_ptr<IVdec>> &esVdec,
                      std::map<ES_U16, std::shared_ptr<IPreprocess>> &preProcess,
                      std::map<ES_U16, std::shared_ptr<EsPostProcess>> &postProcess)
{
    ES_U16 deviceNum = 0;
    std::vector<std::thread> threadVec;
    ES_S32 ret;
    std::mutex mtx;

    ret = ES_NPU_GetNumDevices(&deviceNum);
    for (ES_U16 die = 0; die < deviceNum; die++) {
        threadVec.emplace_back(std::thread(SAMPLE_NPU_COMM_D2DProcessTrd, die, modelPath, std::ref(inputFiles),
                                           outputDir, esVdec[die], preProcess[die], postProcess[die], std::ref(mtx)));
    }

    for (auto &trd : threadVec) {
        if (trd.joinable()) {
            trd.join();
        }
    }

    return ES_SUCCESS;
}


// ==================== 配置参数 ====================
struct Config {
    std::string camera_index = "";              // 摄像头索引
    int max_queue_size = 3;            // 最大帧队列大小
    int target_fps = 30;               // 目标帧率
    bool enable_display = true;        // 是否显示结果
    bool enable_save = false;          // 是否保存结果
    std::string save_path = "output";  // 保存路径
    bool use_direct_memory = true;     // 是否使用直接内存传递
    bool dynamic_resolution = true;    // 是否动态调整分辨率
    int initial_width = 640;           // 初始宽度
    int initial_height = 480;          // 初始高度
};

// ==================== 线程安全资源池 ====================
template <typename T>
class ResourcePool {
public:
    ResourcePool(size_t size, std::function<std::shared_ptr<T>()> creator) {
        for (size_t i = 0; i < size; ++i) {
            pool_.push(creator());
        }
    }

    std::shared_ptr<T> acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return !pool_.empty() || stop_; });
        
        if (stop_) return nullptr;
        
        auto resource = pool_.front();
        pool_.pop();
        return resource;
    }

    void release(std::shared_ptr<T> resource) {
        std::unique_lock<std::mutex> lock(mutex_);
        pool_.push(resource);
        lock.unlock();
        cond_.notify_one();
    }

    void stop() {
        stop_ = true;
        cond_.notify_all();
    }

private:
    std::queue<std::shared_ptr<T>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> stop_{false};
};

// ==================== 帧数据结构 ====================
struct FrameData {
    cv::Mat frame;
    std::vector<uint8_t> encoded_data;  // 编码后的数据
    int64_t frame_id;
    std::chrono::high_resolution_clock::time_point capture_time;
};

// ==================== 性能监控 ====================
class PerformanceMonitor {
public:
    void start_frame() {
        frame_start_ = std::chrono::high_resolution_clock::now();
    }

    void end_frame() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - frame_start_).count();
        frame_times_.push_back(duration);
        
        // 保持最近100帧的数据
        if (frame_times_.size() > 100) {
            frame_times_.erase(frame_times_.begin());
        }
    }

    double get_current_fps() const {
        if (frame_times_.empty()) return 0.0;
        
        double avg_time = std::accumulate(frame_times_.begin(), frame_times_.end(), 0.0) / frame_times_.size();
        return 1000.0 / avg_time;
    }

    void print_stats() const {
        if (frame_times_.empty()) return;
        
        auto min_time = *std::min_element(frame_times_.begin(), frame_times_.end());
        auto max_time = *std::max_element(frame_times_.begin(), frame_times_.end());
        double avg_time = std::accumulate(frame_times_.begin(), frame_times_.end(), 0.0) / frame_times_.size();
        
        std::cout << "Frame processing stats (ms): Min=" << min_time 
                  << ", Max=" << max_time 
                  << ", Avg=" << avg_time
                  << ", FPS=" << (1000.0 / avg_time) << std::endl;
    }

private:
    std::vector<long> frame_times_;
    std::chrono::high_resolution_clock::time_point frame_start_;
};

// ==================== 视频处理器 ====================
class VideoProcessor {
public:
    VideoProcessor(const Config& config,
                 int deviceId, 
                 ES_U32 modelId,
                 std::shared_ptr<IVdec> vdec,
                 std::shared_ptr<IPreprocess> preprocess,
                 std::shared_ptr<EsPostProcess> postprocess)
        : config_(config),
          deviceId_(deviceId),
          modelId_(modelId),
          vdec_(vdec),
          preprocess_(preprocess),
          postprocess_(postprocess),
          frame_pool_(5, []() { return std::make_shared<FrameData>(); }),
          stop_(false) {
        
        if (config.enable_save) {
            cv::utils::fs::createDirectory(config.save_path);
        }
    }

    ~VideoProcessor() {
        stop();
    }

    void start() {
        // 启动捕获线程
        capture_thread_ = std::thread(&VideoProcessor::captureFrames, this);
        
        // 启动处理线程
        process_thread_ = std::thread(&VideoProcessor::processFrames, this);
        
        // 启动显示线程
        if (config_.enable_display) {
            display_thread_ = std::thread(&VideoProcessor::displayFrames, this);
        }
    }

    void stop() {
        stop_ = true;
        frame_pool_.stop();
        
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (process_thread_.joinable()) {
            process_thread_.join();
        }
        if (display_thread_.joinable()) {
            display_thread_.join();
        }
        
        perf_monitor_.print_stats();
    }

    bool isStopped() const {
        return stop_;
    }

private:
    void captureFrames() {
        cv::VideoCapture cap(config_.camera_index);
        if (!cap.isOpened()) {
            std::cerr << "Cannot open video stream" << std::endl;
            return;
        }

        // 设置初始分辨率
        if (config_.dynamic_resolution) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, config_.initial_width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, config_.initial_height);
        }

        int64_t frame_id = 0;
        auto last_frame_time = std::chrono::high_resolution_clock::now();
        
        while (!stop_) {
            perf_monitor_.start_frame();
            
            // 控制帧率
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time).count();
            int delay = std::max(0, static_cast<int>(1000.0 / config_.target_fps - elapsed));
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
            last_frame_time = std::chrono::high_resolution_clock::now();

            // 从资源池获取帧
            auto frame_data = frame_pool_.acquire();
            if (!frame_data) break;  // 资源池已停止

            // 捕获帧
            if (!cap.read(frame_data->frame)) {
                frame_pool_.release(frame_data);
                std::cerr << "Failed to capture frame" << std::endl;
                break;
            }

            // 动态调整分辨率
            if (config_.dynamic_resolution) {
                adjustResolution(cap, perf_monitor_.get_current_fps());
            }

            // 准备数据
            frame_data->frame_id = frame_id++;
            frame_data->capture_time = std::chrono::high_resolution_clock::now();
            
            if (!config_.use_direct_memory) {
                // 编码为JPEG格式
                std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 95};
                cv::imencode(".jpg", frame_data->frame, frame_data->encoded_data, params);
            }

            // 放入处理队列
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (frame_queue_.size() < config_.max_queue_size) {
                    frame_queue_.push(frame_data);
                    queue_cond_.notify_one();
                }
            }
            
            perf_monitor_.end_frame();
        }
    }

    void processFrames() {
        while (!stop_) {
            std::shared_ptr<FrameData> frame_data;
            
            // 从队列获取帧
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cond_.wait(lock, [this]() { return !frame_queue_.empty() || stop_; });
                
                if (stop_) break;
                
                frame_data = frame_queue_.front();
                frame_queue_.pop();
            }

            // 准备输入数据
            std::vector<std::vector<std::string>> input;
            if (config_.use_direct_memory) {
                // 直接内存传递模式 - 需要根据NPU SDK调整
                // 这里假设有将cv::Mat直接转换为NPU输入的方法
                input = prepareInputFromMemory(frame_data->frame);
            } else {
                // 文件模式
                std::string temp_file = saveTempFrame(frame_data->encoded_data, frame_data->frame_id);
                input = {{temp_file}};
            }

            // 执行推理
            ES_S32 ret = SAMPLE_NPU_Sync(deviceId_, modelId_, input, config_.save_path,
                                        vdec_, preprocess_, postprocess_);

            // 处理结果
            if (ret == ES_SUCCESS) {
                // 将结果放入显示队列
                if (config_.enable_display) {
                    std::lock_guard<std::mutex> lock(display_mutex_);
                    display_queue_.push(frame_data);
                    display_cond_.notify_one();
                }

                // 保存结果
                if (config_.enable_save) {
                    saveResult(frame_data->frame, frame_data->frame_id);
                }
            }

            // 释放资源
            if (!config_.use_direct_memory) {
                std::remove(input[0][0].c_str());
            }
            frame_pool_.release(frame_data);
        }
    }

    void displayFrames() {
        cv::namedWindow("YOLOv5 Detection", cv::WINDOW_NORMAL);
        
        while (!stop_) {
            std::shared_ptr<FrameData> frame_data;
            
            // 从显示队列获取帧
            {
                std::unique_lock<std::mutex> lock(display_mutex_);
                display_cond_.wait(lock, [this]() { return !display_queue_.empty() || stop_; });
                
                if (stop_) break;
                
                frame_data = display_queue_.front();
                display_queue_.pop();
            }

            // 计算延迟
            auto now = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - frame_data->capture_time).count();
            
            // 显示帧
            cv::Mat display_frame = frame_data->frame.clone();
            cv::putText(display_frame, 
                       "FPS: " + std::to_string(static_cast<int>(perf_monitor_.get_current_fps())) + 
                       " | Latency: " + std::to_string(latency) + "ms",
                       cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                       cv::Scalar(0, 255, 0), 2);
            
            cv::imshow("YOLOv5 Detection", display_frame);
            
            // 检查用户输入
            if (cv::waitKey(1) == 'q') {
                stop_ = true;
                break;
            }
            
            // 释放资源
            frame_pool_.release(frame_data);
        }
        
        cv::destroyAllWindows();
    }

    // =============== 辅助函数 ===============
    void adjustResolution(cv::VideoCapture& cap, double current_fps) {
        static int width = config_.initial_width;
        static int height = config_.initial_height;
        
        // 简单调整策略：当FPS低于目标时降低分辨率
        if (current_fps < config_.target_fps * 0.9) {
            width = std::max(320, width - 160);
            height = std::max(240, height - 120);
            cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        }
        // 当FPS高于目标时提高分辨率
        else if (current_fps > config_.target_fps * 1.1 && 
                width < config_.initial_width * 2) {
            width = std::min(config_.initial_width * 2, width + 160);
            height = std::min(config_.initial_height * 2, height + 120);
            cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        }
    }

    std::string saveTempFrame(const std::vector<uint8_t>& data, int64_t frame_id) {
        std::string temp_file = "temp_frame_" + std::to_string(frame_id) + ".jpg";
        std::ofstream out(temp_file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        return temp_file;
    }

    void saveResult(const cv::Mat& frame, int64_t frame_id) {
        std::string output_file = config_.save_path + "/result_" + std::to_string(frame_id) + ".jpg";
        cv::imwrite(output_file, frame);
    }

    std::vector<std::vector<std::string>> prepareInputFromMemory(const cv::Mat& frame) {
        // 这里需要根据NPU SDK提供的内存接口实现
        // 假设有一个将cv::Mat直接转换为NPU输入的函数
        // 实际实现可能需要转换为特定的张量格式
        return {{"memory_input"}};  // 伪代码
    }

    // =============== 成员变量 ===============
    Config config_;
    int deviceId_;
    ES_U32 modelId_;
    std::shared_ptr<IVdec> vdec_;
    std::shared_ptr<IPreprocess> preprocess_;
    std::shared_ptr<EsPostProcess> postprocess_;
    
    ResourcePool<FrameData> frame_pool_;
    std::queue<std::shared_ptr<FrameData>> frame_queue_;
    std::queue<std::shared_ptr<FrameData>> display_queue_;
    std::mutex queue_mutex_;
    std::mutex display_mutex_;
    std::condition_variable queue_cond_;
    std::condition_variable display_cond_;
    
    std::atomic<bool> stop_{false};
    std::thread capture_thread_;
    std::thread process_thread_;
    std::thread display_thread_;
    
    PerformanceMonitor perf_monitor_;
};

#endif // SAMPLE_NPU_H