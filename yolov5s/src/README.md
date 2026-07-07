# Npu Samples

## Compiling the samples
chmod a+x build.sh
./build.sh sysroot_path (for fakeroot environment, using: ./build.sh /)
executable binary for sample_npu is in the folder 'build' in the current directory
## Running the samples
The sample_npu supports seven samples to show the usage of npu runtime API.
### Synchronous tasks
It is the simplest usage of npu runtime for submitting synchronous tasks, it is also the first samples in sample_npu.
### Asynchronous task
The second sample is the usage of submitting asynchronous tasks, which is more complex. The user need to create context and stream to manage their tasks, and create a thread to query the state of their tasks.
### Contexts
The third sample is the usage of submitting asynchronous tasks in multiple contexts.
### Streams
The fourth sample is the usage of submitting asynchronous tasks in multiple streams.
### Models
The fifth sample shows the ability of multiple models to work simultaneously for npu runtime.
### Composite model
The sixth sample is the usage of submit picture one by one to the composite model.
### D2D
The seventh sample shows the usage of API on D2D machine recommended by us.
### Arguments
-s(--sample) Set the type of sample.(1-Synchronous tasks, 2-Asynchronous task, 3-Contexts, 4-Streams, 5-Models, 6-NBatch, 7-D2D)
-m(--model) Set the directory of model, it will search the *.model in the directory.
-i(--input) Set the directory of input pictures.
-o(--output) Set the directory to save results.
-p(--pre_process) Set the pre-process config file path.
-q(--post_process) Set the post-process config file path.
-t(--classes) Set the file path of the table of classes id and label.
-n(--number) Set the numbers of stream and context.(It is only used in streams and contexts samples).
example: sample_npu -s 1 -o ./out(Use default model and config file)
         sample_npu -s 2 -m /opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3 -i /opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/input/pictures -p /opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/es_yolov3_pre_process.json -q /opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/es_yolov3_post_process.json -t /opt/eswin/sample-code/npu_sample/npu_runtime_sample/models/yolov3/es_yolov3_classes.txt -o ./out
note:
-m(--model) supports multiple inputs of model directory which is separated by commas.
The sample(1~5) will use the model which include keyword "latency" in model name when there are multiple models in the model directory.
The format of config files and input pictures can refer to yolov3 released by eswin.
The yolov3 will output the pictures with bounding boxes and the classification will save result if the -o argument is set.