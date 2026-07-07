#!/bin/bash
set -e

sys_root_dir=null

function usage() {
    echo -e "Usage:\n$0 <sys_root_dir>"
}

function main() {
    if [ $# -le 0 ]; then
        usage
        exit 1
    fi

    sys_root_dir=$1

    if [ ! -d build ]; then
        mkdir -p build
    else
        rm -rf build/*
    fi

    cd build

    : "${CROSS_COMPILE:?CROSS_COMPILE is not set}"

    # Run CMake with the specified cross compiler before project() is evaluated.
    cmake .. \
        -DNOT_BUILDROOT=ON \
        -DSYSROOT_PATH=${sys_root_dir} \
        -DCMAKE_SYSROOT=${sys_root_dir} \
        -DCMAKE_FIND_ROOT_PATH=${sys_root_dir} \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_C_FLAGS="-B${sys_root_dir}/usr/lib/riscv64-linux-gnu" \
        -DCMAKE_CXX_FLAGS="-B${sys_root_dir}/usr/lib/riscv64-linux-gnu" \
        -DCMAKE_EXE_LINKER_FLAGS="-B${sys_root_dir}/usr/lib/riscv64-linux-gnu -Wl,-rpath-link,${sys_root_dir}/usr/lib/riscv64-linux-gnu -Wl,-rpath-link,${sys_root_dir}/usr/lib" \
        -DCMAKE_C_COMPILER=${CROSS_COMPILE}gcc \
        -DCMAKE_CXX_COMPILER=${CROSS_COMPILE}g++

    make
}

main $@
