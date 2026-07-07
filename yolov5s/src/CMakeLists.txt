# Copyright (c) 2023 ESWIN Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

cmake_minimum_required(VERSION 3.16.1)
project(es_npu_samples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "-Werror -O2")
set(CMAKE_CXX_FLAGS "-Werror -O2")

# set(CMAKE_SHARED_LINKER_FLAGS
#     "${CMAKE_SHARED_LINKER_FLAGS} -L/opt/riscv/lib/gcc/riscv64-unknown-linux-gnu/14.2.0 -lstdc++"
# )

if(NOT DEFINED SYSROOT_PATH)
    message(
        FATAL_ERROR
            "SYSROOT_PATH must be set. Use -DSYSROOT_PATH=/path/to/sysroot")
endif()

if(NOT_BUILDROOT)
    set(CMAKE_C_COMPILER "$ENV{CROSS_COMPILE}gcc")
    set(CMAKE_CXX_COMPILER "$ENV{CROSS_COMPILE}g++")
    set(CMAKE_SYSROOT ${SYSROOT_PATH})
    set(EIC7700_BOARD "")
endif()

set(NPU_SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NPU_SAMPLE sample_npu)

message(STATUS "SYSROOT_PATH: ${SYSROOT_PATH}")
message(STATUS "EIC7700_BOARD: ${EIC7700_BOARD}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "NOT_BUILDROOT: ${NOT_BUILDROOT}")

include_directories(
    ${NPU_SAMPLE_DIR}
    ${NPU_SAMPLE_DIR}/utils
    ${NPU_SAMPLE_DIR}/utils/json
    ${NPU_SAMPLE_DIR}/utils/preprocess
    ${NPU_SAMPLE_DIR}/utils/vdec
    ${NPU_SAMPLE_DIR}/utils/vdec/common
    ${NPU_SAMPLE_DIR}/utils/postprocess
    ${SYSROOT_PATH}/usr/include/essdk
    ${SYSROOT_PATH}/usr/include/opencv4)

if(NOT "${EIC7700_BOARD}" STREQUAL "")
    include_directories(${EIC7700_BOARD}/package/essdk/include)
    file(COPY ${EIC7700_BOARD}/system/kernel/include/uapi/linux/es_vb_user.h
         DESTINATION ${SYSROOT_PATH}/usr/include/essdk)
endif()

file(
    GLOB
    src_files
    ${NPU_SAMPLE_DIR}/*.cpp
    ${NPU_SAMPLE_DIR}/utils/*.cpp
    ${NPU_SAMPLE_DIR}/utils/json/*.cpp
    ${NPU_SAMPLE_DIR}/utils/postprocess/*.cpp
    ${NPU_SAMPLE_DIR}/utils/vdec/*.cpp
    ${NPU_SAMPLE_DIR}/utils/vdec/common/*.cpp
    ${NPU_SAMPLE_DIR}/utils/preprocess/*.cpp)

add_executable(${NPU_SAMPLE} ${src_files})

if(NOT_BUILDROOT)
    target_link_directories(
        ${NPU_SAMPLE}
        PUBLIC
        ${SYSROOT_PATH}/usr/lib/essdk
        ${SYSROOT_PATH}/usr/lib/riscv64-linux-gnu/
        ${SYSROOT_PATH}/usr/lib/riscv64-linux-gnu/lapack
        ${SYSROOT_PATH}/usr/lib/riscv64-linux-gnu/blas
        ${SYSROOT_PATH}/usr/lib/)
else()
    target_link_directories(${NPU_SAMPLE} PUBLIC ${SYSROOT_PATH}/usr/lib/essdk)
endif()

if(NOT "${EIC7700_BOARD}" STREQUAL "")
    target_link_directories(${NPU_SAMPLE} PUBLIC
                            ${EIC7700_BOARD}/package/essdk/lib)
endif()

target_link_options(${NPU_SAMPLE} PRIVATE "-Wl,--as-needed")
target_link_libraries(
    ${NPU_SAMPLE}
    PRIVATE -lpthread
            -ledla_runtime
            -les_sys
            -les-numa
            -les_memory
            -les_dsp_runtime_ll
            -les_accelerator_kit
            -les_vdec
            -les_vps
            -les_memcp
            -les_memory
            -lmanager
            -lopencv_core
            -lopencv_imgproc
            -lopencv_imgcodecs
            -lopencv_videoio
            -lopencv_highgui)

install(TARGETS ${NPU_SAMPLE} RUNTIME DESTINATION bin)
