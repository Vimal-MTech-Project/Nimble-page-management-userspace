#!/bin/bash

BENCH_NAME="559.pmniGhost"

if [[ "x${BENCH_SIZE}" == "x8GB"  ]]; then
# ~8GB memory, ~25 sec
nx=1000
ny=1000
nz=175
else
# default ~8GB memory, ~25 sec
nx=1000
ny=1000
nz=175
fi

BENCH_RUN="./miniGhost_base.gnu --scaling 1 --nx $nx --ny $ny --nz $nz --num_vars 5 \
--percent_sum 0 --num_spikes 1 --num_tsteps 5 --stencil 21 --comm_method 10 --error_tol \
5 --debug_grid 1 --report_diffusion 10" 

#${BENCH_RUN} &>/dev/null
#echo ${CPUID} ${BENCH_NAME}
