#!/bin/bash

# Turn-off AutoNUMA
sudo sysctl kernel.numa_balancing=0

# Hotplug PMEM region dax5.0 as a system ram
# You may need to change this as per your setup
daxctl reconfigure-device dax5.0 --mode=system-ram

# enable printing debugging info
echo 8 > /proc/sys/kernel/printk

# Always try to allocate THP
echo always > /sys/kernel/mm/transparent_hugepage/enabled
sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
sudo sysctl kernel.enable_nt_exchange_page=0
sudo sysctl kernel.enable_nt_page_copy=0

# You may need to change this as per your setup.
# In our setup the memory of NUMA node 2 is backed by DRAM
export FAST_NODE=2
# You may need to change this as per your setup.
# In our setup the memory of NUMA node 9 is backed by PMEM
export SLOW_NODE=9
# STATS_PERIOD controls at how many seconds the 
# victim page selection and migration happends
export STATS_PERIOD=5
export MOVE_HOT_AND_COLD_PAGES=no
export SHRINK_PAGE_LISTS=yes
export FORCE_NO_MIGRATION=no
# Number of times the experiment gets repeated
export NR_RUNS=5
# This parameter controls number of pages simultaneously gets 
# migrated using concurrent page migration mechanism
export MIGRATION_BATCH_SIZE=8
# Controls the number of threads used in page migration(and page copy)
export MIGRATION_MT=1
export PREFER_FAST_NODE=yes
# controls the total memory requirement of benchmarks
export BENCH_SIZE="8GB"
# no memory will be allocated from NUMA nodes in blocked_nodes list
# Every node except `fast_node` and `slow_node` should have it's id listed in 
# blocked_nodes
export blocked_nodes=(0 1 3 7 8 10)
#MEM_SIZE_LIST="unlimited"
# Values in MEM_SIZE_LIST are in GB and represents the max limit on 
# the application memory which can be allocated on the fast node
MEM_SIZE_LIST=(4)

RES_FOLDER="results-mm-manage-fast-${MEM_SIZE}-${MIGRATION_MT}-threads"

BENCHMARK_LIST="559.pmniGhost 504.polbm graph500-omp 503.postencil 553.pclvrleaf 555.pseismic"
PAGE_REPLACEMENT_SCHEMES=(all-local-access rpdaa-nt rpdaa nt nimble-best nimble-default stock-linux all-remote-access)
#export NO_MIGRATE=""

#THREAD_LIST="20"
THREAD_LIST="16"
MEMHOG_THREAD_LIST=$(seq 0 0)

read -a BENCH_ARRAY <<< "${BENCHMARK_LIST}"


#THRESHOLD=`cat /proc/zoneinfo | grep -A 5 "Node 1" | grep high | awk '{print $2}' `
#THRESHOLD=$((-THRESHOLD/64))

#sudo sysctl vm/times_kmigrationd_threshold=${THRESHOLD}

trap "./create_die_stacked_mem.sh remove; ./cleanup.sh; exit" INT


sudo sysctl vm/migration_batch_size=${MIGRATION_BATCH_SIZE}
sudo sysctl vm/limit_mt_num=${MIGRATION_MT}
if test ${SCAN_DIVISOR} -gt 0 ;  then
sudo sysctl vm/scan_divisor=${SCAN_DIVISOR}
fi


for i in $(seq 1 ${NR_RUNS});
do
	BENCH_ARRAY=( $(shuf -e "${BENCH_ARRAY[@]}") )
	PAGE_REPLACEMENT_SCHEMES=( $(shuf -e "${PAGE_REPLACEMENT_SCHEMES[@]}") )
	
	# shuffle the arrays show as to avoid the impacts of nvm-wear leveling
	printf "BENCH_ARRAY: [%s]" "${BENCH_ARRAY[@]}"
	printf "PAGE_REPLACEMENT_SCHEMES: [%s]" "${PAGE_REPLACEMENT_SCHEMES[@]}"

	for MEM_SIZE in ${MEM_SIZE_LIST};
	do

		if [[ "x${MEM_SIZE}" != "xunlimited" ]]; then
			MEM_SIZE="${MEM_SIZE}GB"
		fi

		RES_FOLDER="results-mm-manage-prefer-fast-${MEM_SIZE}-policy"
		if [ ! -d "${RES_FOLDER}" ]; then
		echo "Prepare folders"
		mkdir -p ${RES_FOLDER}
		fi

		for B_IDX in $(seq 0 $((${#BENCH_ARRAY[@]}-1)));
		do
			#echo ${BENCH_ARRAY[${B_IDX}]}" at "${BENCH_FOLDER_ARRAY[${B_IDX}]}
			export BENCH=${BENCH_ARRAY[${B_IDX}]}

			if [ ! -d "${RES_FOLDER}/${BENCH}" ]; then
				mkdir -p ${RES_FOLDER}/${BENCH}
			fi

			for SCHEME in ${PAGE_REPLACEMENT_SCHEMES[@]};
			do

				if [[ "x${MEM_SIZE}" != "xunlimited" ]]; then
					./create_die_stacked_mem.sh node_size ${FAST_NODE} ${MEM_SIZE}
				else
					./create_die_stacked_mem.sh
					export MOVE_HOT_AND_COLD_PAGES=yes
				fi
				#sudo sysctl vm.enable_prefetcher=0
				export SCHEME=${SCHEME}
				echo "Configuration: ${SCHEME}"

				for THREAD in ${THREAD_LIST};
				do

					for MEMHOG_THREADS in ${MEMHOG_THREAD_LIST};
					do
						export MEMHOG_THREADS=${MEMHOG_THREADS}

						if [ "${SCHEME}" == "all-remote-access" ]; then
							export NO_MIGRATION=yes
							sudo sysctl vm.sysctl_enable_thp_migration=1

							./run_bench.sh ${THREAD};
						fi

						if [ "${SCHEME}" == "all-local-access" ]; then
							export NO_MIGRATION=yes
							sudo sysctl vm.sysctl_enable_thp_migration=1

							./run_bench.sh ${THREAD};
						fi

						if [ "${SCHEME}" == "non-thp-migration" ]; then
							export NO_MIGRATION=no

							sudo sysctl vm.sysctl_enable_thp_migration=0
							./run_bench.sh ${THREAD};
							sudo sysctl vm.sysctl_enable_thp_migration=1
						fi

						if [ "${SCHEME}" == "thp-migration" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1

							./run_bench.sh ${THREAD};
						fi
						if [ "${SCHEME}" == "opt-migration" ] || [ "${SCHEME}" == "concur-only-opt-migration" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1

							./run_bench.sh ${THREAD};
						fi
						if [ "${SCHEME}" == "exchange-pages" ] || [ "${SCHEME}" == "basic-exchange-pages" ] || [ "${SCHEME}" == "concur-only-exchange-pages" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1

							./run_bench.sh ${THREAD};
						fi
						if [ "${SCHEME}" == "pmem-optimized" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=1
							./run_bench.sh ${THREAD};
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
						fi
						if [ "${SCHEME}" == "non-thp-exchange-pages" ]; then
							export NO_MIGRATION=no

							sudo sysctl vm.sysctl_enable_thp_migration=0
							./run_bench.sh ${THREAD};
							sudo sysctl vm.sysctl_enable_thp_migration=1
						fi

						if [ "${SCHEME}" == "rpdaa-nt" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=1
							sudo sysctl kernel.enable_nt_exchange_page=1
							sudo sysctl kernel.enable_nt_page_copy=1
							./run_bench.sh ${THREAD};
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
							sudo sysctl kernel.enable_nt_exchange_page=0
							sudo sysctl kernel.enable_nt_page_copy=0
						fi

						if [ "${SCHEME}" == "nt" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1
							sudo sysctl kernel.enable_nt_exchange_page=1
							sudo sysctl kernel.enable_nt_page_copy=1
							./run_bench.sh ${THREAD};
							sudo sysctl kernel.enable_nt_exchange_page=0
							sudo sysctl kernel.enable_nt_page_copy=0
						fi

						if [ "${SCHEME}" == "rpdaa" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm.sysctl_enable_thp_migration=1
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=1
							./run_bench.sh ${THREAD};
							sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
						fi

						if [ "${SCHEME}" == "nimble-default" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm/limit_mt_num=4
							sudo sysctl vm.sysctl_enable_thp_migration=1
							./run_bench.sh ${THREAD};
							sudo sysctl vm/limit_mt_num=${MIGRATION_MT}
						fi

						if [ "${SCHEME}" == "nimble-best" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm/limit_mt_num=1
							sudo sysctl vm.sysctl_enable_thp_migration=1
							./run_bench.sh ${THREAD};
							sudo sysctl vm/limit_mt_num=${MIGRATION_MT}
						fi

						if [ "${SCHEME}" == "stock-linux" ]; then
							export NO_MIGRATION=no
							sudo sysctl vm/limit_mt_num=1
							sudo sysctl vm.sysctl_enable_thp_migration=0
							./run_bench.sh ${THREAD};
							sudo sysctl vm.sysctl_enable_thp_migration=1
							sudo sysctl vm/limit_mt_num=${MIGRATION_MT}
						fi

						sleep 5
						./create_die_stacked_mem.sh remove

					done # MEMHOG
				done # THREAD LIST
			done # SCHEME

			mv result-${BENCH_ARRAY[${B_IDX}]}-* ${RES_FOLDER}/${BENCH}/
		done # BENCH

	done # MEM_SIZE
done # i

./cleanup.sh
