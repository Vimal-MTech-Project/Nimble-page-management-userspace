#!/bin/bash

if [[ -z "$SOURCE_NODE" ]]; then
	# default to DRAM to DRAM migration
	SOURCE_NODE=2
fi

if [[ -z "$SOURCE_CPU_NODE" ]]; then
	SOURCE_CPU_NODE=2
fi

if [[ -z "$DESTINATION_NODE" ]]; then
	# default to DRAM to DRAM migration
	DESTINATION_NODE=3
fi

echo "SOURCE_NODE: $SOURCE_NODE"
echo "SOURCE_CPU_NODE: $SOURCE_CPU_NODE"
echo "DESTINATION_NODE: $DESTINATION_NODE"

PAGE_LIST=`seq 0 9`
COPY_METHOD="mt"
MULTI="1 2 4 8 16"

PERF_EVENT_LIST=("bandwidth(GB/s)" unc_m_pmm_rpq_occupancy_all_0 unc_m_pmm_wpq_occupancy_all_0 unc_m_pmm_rpq_inserts_0 unc_m_pmm_wpq_inserts_0)
PERF_EVENT_LIST_STR="bandwidth(GB/s),unc_m_pmm_rpq_occupancy_all_0,unc_m_pmm_wpq_occupancy_all_0,unc_m_pmm_rpq_inserts_0,unc_m_pmm_wpq_inserts_0"

PMEM_OPTIMIZED_SUFFIX_STR=(not_pmem_optimized pmem_optimized)

if [ ! -d thp_verify ]; then
	mkdir thp_verify
fi

if [ ! -d stats_4kb ]; then
	mkdir stats_4kb
fi


for I in `seq 1 5`; do
	echo "[6]=========starting measurement number $I.=========="
	for MT in ${MULTI}; do
		sudo sysctl vm.limit_mt_num=${MT}
		for METHOD in ${COPY_METHOD}; do
			if [[ "x${METHOD}" == "xmt" && "x${MT}" == "x1" ]]; then
				PARAM="st"
			else
				PARAM=${METHOD}
			fi
			for N in ${PAGE_LIST}; do
				NUM_PAGES=$((1<<N))
				for pmemOptimized in `seq 0 0`; do
					sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=$pmemOptimized
					echo "[6]NUM_PAGES: "${NUM_PAGES}", METHOD: "${PARAM}", BATCH: "${BATCH}", MT: "${MT}", pmemOptimized: "${pmemOptimized}

					if [[ "x${I}" == "x1" ]]; then
						ocperf stat -x, -o ocperf_temp_output.txt -e UNC_M_PMM_RPQ_OCCUPANCY.ALL,UNC_M_PMM_WPQ_OCCUPANCY.ALL,UNC_M_PMM_RPQ_INSERTS,UNC_M_PMM_WPQ_INSERTS numactl -N ${SOURCE_CPU_NODE} -m ${SOURCE_NODE} ./non_thp_move_pages ${NUM_PAGES} ${PARAM} ${SOURCE_NODE} ${DESTINATION_NODE} ${pmemOptimized} 2>./thp_verify/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]} | grep -A 3 "\(Total_cycles\|Test successful\)" > ./stats_4kb/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]}
						echo $PERF_EVENT_LIST_STR > ./stats_4kb/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]}_perf_stats
					else
						ocperf stat -x, -o ocperf_temp_output.txt -e UNC_M_PMM_RPQ_OCCUPANCY.ALL,UNC_M_PMM_WPQ_OCCUPANCY.ALL,UNC_M_PMM_RPQ_INSERTS,UNC_M_PMM_WPQ_INSERTS numactl -N ${SOURCE_CPU_NODE} -m ${SOURCE_NODE} ./non_thp_move_pages ${NUM_PAGES} ${PARAM} ${SOURCE_NODE} ${DESTINATION_NODE} ${pmemOptimized} 2>./thp_verify/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]} | grep -A 3 "\(Total_cycles\|Test successful\)" >> ./stats_4kb/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]}
						echo "" >> ./stats_4kb/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]}_perf_stats
					fi

					for i in `seq 0 4`; do
						line=$(cat ocperf_temp_output.txt | grep -i ${PERF_EVENT_LIST[$i]})
						splittedArr=(${line//,/ })
						currentValue=${splittedArr[0]}
						echo -n "$currentValue," >> ./stats_4kb/${METHOD}_${MT}_4kb_page_order_${N}_${PMEM_OPTIMIZED_SUFFIX_STR[$pmemOptimized]}_perf_stats
						# sums[$i]=$(echo "print(${sums[$i]}+$currentValue)" | python3)
						# sums[$i]=$((${sums[$i]}+$currentValue))
						# echo ${sums[$i]}
					done

					rm ocperf_temp_output.txt
					sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
					sleep 1
				done
			done
		done
	done
done


