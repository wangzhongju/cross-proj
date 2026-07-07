# Yolov5s Model: Using Quantized Models to Generate and Run Offline Models

## 1. Overview

This document provides instructions on how to compile a quantized model to generate an offline model. The quantized models are located in the `pc-models` directory, and this guide will walk you through the process of compiling the model and testing its accuracy.

### Directory structure
```
yolov5s/
├── bin
│   └── sample_npu
├── models
│   └── 416x416
│   └── 640x640
├── README.md
└── src
    ├── build.sh
    ├── CMakeLists.txt
    ├── README.md
    ├── sample_npu_comm.cpp
    ├── sample_npu_comm.h
    ├── sample_npu.cpp
    └── utils
```
## 2. Using Quantized Models to Generate Offline Models

### 2.1 Docker Setup and Model Compilation
Follow these steps to generate the offline model from the quantized model.

1. Switch to the directory where the README.md is located.
```
$ cd /path/to/es-hw-models/yolov5/yolov5s
```
2. Load the Docker image and run the simulator to compile the model.
```
$ docker load -i ../../../../../estools/EsNNTools/esaac_essimulator_docker.tar

$ docker run --rm --privileged -it \
            -v "$(dirname "$(dirname "$(dirname "$(pwd)")")")":/home/eswin/detection \
            -u root -w "/home/eswin" esaac_essimulator:latest bash -c "/home/eswin/EsAAC \
            --input-model detection/pc-models/yolov5/yolov5s/416x416/quantized-model/quantized.onnx \
            --output-dir detection/es-hw-models/yolov5/yolov5s/models/416x416 \
            --loadable-name git_yolov5s_416_mix_1x3x416x416_dyn \
            --input-nodes images \
            --input-shapes 1,3,416,416 \
            --run-device npu \
            --quant-stats detection/pc-models/yolov5/yolov5s/416x416/quantized-model/table.json"
```
### Explanation of parameters:

+ **--input-model**: The path to the quantized model (in ONNX format).
+ **--output-dir**: The output directory where the compiled offline model will be saved.
+ **--loadable-name**: The name of the loadable offline model.
+ **--input-nodes**: The name of the input node for the model.
+ **--input-shapes**: The shape of the input tensor (for example, 1,3,416,416).
+ **--run-device**: The device on which the model will run, in this case, npu (could be npu, dsp or all).
+ **--quant-stats**: The path to the quantization statistics file (table.json).
you can modify those parameters to compile 640x640

### 2.2 Generate postprocess.json and preprocess.json

```
$ python ../../../pc-models/yolov5/yolov5s/postprocess_gen.py --version yolov5 --path ../../../pc-models/yolov5/yolov5s/416x416/quantized-model/
$ cp ../../../pc-models/yolov5/yolov5s/416x416/quantized-model/preprocess.json models/416x416/
$ cp ../../../pc-models/yolov5/yolov5s/416x416/quantized-model/postprocess.json models/416x416/
```
This will generate postprocess.json and preprocess.json files which are necessary for correct inference and post-processing on the target hardware.

## 3. Running the Offline Model

Once the model is compiled and ready, follow these steps to run the offline model on your target device.
before you start, you should modify the config of postprocess.json: score_threshold=0.5, iou_thres=0.6
### 3.1 Copy the Model Files and Executables

#### Copy the compiled models and sample_npu executable to the target system:
```
$ scp -r /path/to/es-hw-models/yolov5/yolov5s eswin@<target-hostname>:/home/eswin
```

### 3.2 Running the Model (on the target)
After transferring the files, SSH into your target system and run the sample_npu executable with the appropriate arguments.

```
eswin@rockos-eswin:~$ mkdir output
eswin@rockos-eswin:~$ ls
yolov5s output
eswin@rockos-eswin:~$ yolov5s/bin/sample_npu -s 1 -m yolov5s/models/416x416/ -i yolov5s/models/416x416/pictures/ -o output
yolov5s/models/416x416/pictures/input0/000000006818.jpg: box count(0)
yolov5s/models/416x416/pictures/input0/000000016228.jpg: box count(4)
class_id: 17.000000( ('horse')), score: 0.898926, x_min: 215.750000, y_min: 168.250000, x_max: 352.500000, y_max: 290.500000
class_id: 13.000000( ('bench')), score: 0.623047, x_min: 333.000000, y_min: 214.750000, x_max: 407.000000, y_max: 254.750000
class_id: 0.000000( ('person')), score: 0.570801, x_min: 49.125000, y_min: 192.000000, x_max: 62.750000, y_max: 235.875000
class_id: 0.000000( ('person')), score: 0.569824, x_min: 129.000000, y_min: 188.500000, x_max: 147.500000, y_max: 235.875000
yolov5s/models/416x416/pictures/input0/000000017627.jpg: box count(8)
class_id: 2.000000( ('car')), score: 0.871582, x_min: 171.000000, y_min: 202.000000, x_max: 246.250000, y_max: 249.625000
class_id: 2.000000( ('car')), score: 0.772949, x_min: 386.500000, y_min: 209.875000, x_max: 416.000000, y_max: 232.750000
class_id: 2.000000( ('car')), score: 0.761719, x_min: 264.250000, y_min: 204.000000, x_max: 315.000000, y_max: 235.875000
class_id: 2.000000( ('car')), score: 0.739746, x_min: 313.000000, y_min: 205.250000, x_max: 366.750000, y_max: 237.625000
class_id: 2.000000( ('car')), score: 0.709961, x_min: 112.000000, y_min: 203.250000, x_max: 176.250000, y_max: 246.250000
class_id: 0.000000( ('person')), score: 0.637695, x_min: 95.312500, y_min: 195.000000, x_max: 109.875000, y_max: 245.000000
class_id: 7.000000( ('truck')), score: 0.561035, x_min: 15.867188, y_min: 195.750000, x_max: 82.437500, y_max: 264.500000
class_id: 2.000000( ('car')), score: 0.532227, x_min: 346.000000, y_min: 206.375000, x_max: 379.000000, y_max: 234.375000
yolov5s/models/416x416/pictures/input0/000000025560.jpg: box count(2)
class_id: 15.000000( ('cat')), score: 0.850586, x_min: 86.250000, y_min: 170.625000, x_max: 342.750000, y_max: 274.000000
class_id: 62.000000( ('tv')), score: 0.761230, x_min: 96.375000, y_min: 49.125000, x_max: 372.000000, y_max: 231.375000
yolov5s/models/416x416/pictures/input0/000000035197.jpg: box count(3)
class_id: 0.000000( ('person')), score: 0.848633, x_min: 76.562500, y_min: 4.375000, x_max: 236.875000, y_max: 276.500000
class_id: 36.000000( ('skateboard')), score: 0.757324, x_min: 68.187500, y_min: 229.750000, x_max: 235.875000, y_max: 297.250000
class_id: 0.000000( ('person')), score: 0.605957, x_min: 309.500000, y_min: 154.375000, x_max: 328.750000, y_max: 203.875000
yolov5s/models/416x416/pictures/input0/000000037777.jpg: box count(1)
class_id: 72.000000( ('refrigerator')), score: 0.884277, x_min: 352.750000, y_min: 162.125000, x_max: 416.000000, y_max: 340.000000
yolov5s/models/416x416/pictures/input0/000000039956.jpg: box count(2)
class_id: 59.000000( ('bed')), score: 0.836914, x_min: 0.000000, y_min: 203.375000, x_max: 242.500000, y_max: 346.500000
class_id: 56.000000( ('chair')), score: 0.834961, x_min: 336.500000, y_min: 190.250000, x_max: 413.500000, y_max: 277.000000
yolov5s/models/416x416/pictures/input0/000000041888.jpg: box count(3)
class_id: 14.000000( ('bird')), score: 0.892578, x_min: 129.500000, y_min: 185.125000, x_max: 213.750000, y_max: 255.375000
class_id: 14.000000( ('bird')), score: 0.885254, x_min: 190.000000, y_min: 210.500000, x_max: 253.125000, y_max: 281.000000
class_id: 14.000000( ('bird')), score: 0.695312, x_min: 260.750000, y_min: 217.750000, x_max: 343.250000, y_max: 286.000000
yolov5s/models/416x416/pictures/input0/000000058636.jpg: box count(0)
yolov5s/models/416x416/pictures/input0/000000063154.jpg: box count(2)
class_id: 0.000000( ('person')), score: 0.839355, x_min: 198.500000, y_min: 157.625000, x_max: 251.875000, y_max: 199.750000
class_id: 37.000000( ('surfboard')), score: 0.634277, x_min: 160.125000, y_min: 191.125000, x_max: 212.625000, y_max: 214.500000
```
you can get the detect result in output, these images should be consistent with those in models/416x416/result

## 4. Testing the Model's Accuracy (on the target)
For test the model's accuracy, you should modify the config of postprocess.json(score_threshold=0.01, iou_thres=0.6)
and download the val2017 from <https://images.cocodataset.org/zips/val2017.zip>.
- 1. Create an output directory for saving the results:
```
eswin@rockos-eswin:~$ mkdir output
```
- 2. Run the sample_npu executable on the input dataset and store the results in the output directory:
```
eswin@rockos-eswin:~$ mkdir -p val2017/input0
eswin@rockos-eswin:~$ mv val2017/*.jpg val2017/input0/
eswin@rockos-eswin:~$ yolov5s/bin/sample_npu -s 1 -m yolov5s/models/416x416/ -i val2017/ -o output
```
- 3. After running the inference, check the output directory for the results:
```
eswin@rockos-eswin:~$  ls output/ | grep -i "prediction.json"
prediction.json
```

### 4.1 Calculate Accuracy
Copy the generated prediction.json to models/416x416/prediction.json, download instances_val2017.json from <https://images.cocodataset.org/annotations/annotations_trainval2017.zip>, then calculate the model's accuracy using the provided Python script.
```
$ python ../../../pc-models/tools/map_coco.py --groundtruth instances_val2017.json --prediction models/416x416/prediction.json
```

This will compare the predictions in prediction.json against the ground truth data in instances_val2017.json and output the accuracy metrics.

## Results:
| Model Name       | FP32 Metrics | quant Metrics |
|------------------|--------------|---------------|
| yolov5s_416_416 | 34.0  | 32.8   |
| yolov5s_640_640 | 37.0  | 35.8   |

## 5. Conclusion
With this guide, you should now be able to compile, run, and test the accuracy of the quantized yolov5s model on your target device. If you encounter any issues during the process, refer to the additional documentation or contact technical support.
