#!/bin/bash

BENCH_NAME="553.pclvrleaf"

if [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
cat clover.in_8GB > clover.in; #8GB memory
else
cat clover.in_8GB > clover.in; #8GB memory
fi

BENCH_RUN="./pclvrleaf_base.gnu"

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
