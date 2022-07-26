#!/bin/bash

BENCH_NAME="503.postencil"

if [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
BENCH_RUN="./stencil_exe_base.gnu 1024 1024 1024 15" #8GB memory
else
BENCH_RUN="./stencil_exe_base.gnu 1024 1024 1024 15" #8GB memory
fi

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
