#!/bin/bash

BENCH_NAME="504.polbm"

if [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
BENCH_RUN="./polbm 10 reference.dat 0 0 100_100_2600_ldc.of"; # ~8GB memory, 36 seconds on all-local
else
BENCH_RUN="./polbm 10 reference.dat 0 0 100_100_2600_ldc.of"; # ~8GB memory, 36 seconds on all-local
fi

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
