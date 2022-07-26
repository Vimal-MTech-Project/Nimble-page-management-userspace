#!/bin/bash

BENCH_NAME="555.pseismic"

if [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
BENCH_RUN="./pseismic 168 512 512 2 5 1000" # ~8GB memory, ~25 sec
else
BENCH_RUN="./pseismic 168 512 512 2 5 1000" # ~8GB memory, ~25 sec
fi

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
