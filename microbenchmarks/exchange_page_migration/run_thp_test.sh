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

if [[ -z "$CONFIGURATION_NAME" ]]; then
	# default to DRAM to DRAM migration
	CONFIGURATION_NAME="dram to dram"
fi

echo "SOURCE_NODE: $SOURCE_NODE"
echo "SOURCE_CPU_NODE: $SOURCE_CPU_NODE"
echo "DESTINATION_NODE: $DESTINATION_NODE"

# PAGE_LIST=`seq 9 9`
PAGE_LIST=`seq 0 9`
COPY_METHOD="seq mt"
COPY_METHOD="mt"
# BATCH_MODE="batch no_batch"
BATCH_MODE="batch"
MULTI="1 2 4 8 16"
# MULTI="1 2"

PERF_EVENT_LIST=("bandwidth(GB/s)" unc_m_pmm_rpq_occupancy_all_0 unc_m_pmm_wpq_occupancy_all_0 unc_m_pmm_rpq_inserts_0 unc_m_pmm_wpq_inserts_0)
PERF_EVENT_LIST_STR="bandwidth(GB/s),unc_m_pmm_rpq_occupancy_all_0,unc_m_pmm_wpq_occupancy_all_0,unc_m_pmm_rpq_inserts_0,unc_m_pmm_wpq_inserts_0"

CONFIG_ID=($CONFIGURATION_NAME"_NT_OFF_RPDAA_OFF" $CONFIGURATION_NAME"_NT_OFF_RPDAA_ON" $CONFIGURATION_NAME"_NT_ON_RPDAA_OFF" $CONFIGURATION_NAME"_NT_ON_RPDAA_ON")
NT=(0 0 1 1)
RPDAA=(0 1 0 1)
OPTIMIZATION_CONFIG=(0 1 2 3)

if [ ! -d thp_verify ]; then
	mkdir thp_verify
fi

if [ ! -d stats_2mb ]; then
	mkdir stats_2mb
fi

for I in `seq 1 5`; do
	echo "[4]=========starting measurement number $I.=========="
	for MT in ${MULTI}; do
		sudo sysctl vm.limit_mt_num=${MT}
		for BATCH in ${BATCH_MODE}; do
			for METHOD in ${COPY_METHOD}; do
				PARAM=${METHOD}
				if [[ "x${METHOD}" == "xseq" && "x${MT}" != "x1" ]]; then
					continue
				fi
				# if [[ "x${METHOD}" == "xmt" && "x${MT}" == "x1" ]]; then
				# 	continue
				# fi
				for N in ${PAGE_LIST}; do
					NUM_PAGES=$((1<<N))
					for pmemOptimized in ${OPTIMIZATION_CONFIG[@]}; do
						# pmem optimization requires batch multithreaded exchange
						# if [[ "x${BATCH}" == "xno_batch" && "x${pmemOptimized}" == "x1" ]]; then
						# 	continue
						# fi
						
						echo "[4]NUM_PAGES: "${NUM_PAGES}", METHOD: "${PARAM}", BATCH: "${BATCH}", MT: "${MT}", pmemOptimized: "${pmemOptimized}
						
						sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=${RPDAA[$pmemOptimized]}
						sudo sysctl kernel.enable_nt_exchange_page=${NT[$pmemOptimized]}


						if [[ "x${I}" == "x1" ]]; then
							ocperf stat -x, -o ocperf_temp_output.txt -e UNC_M_PMM_RPQ_OCCUPANCY.ALL,UNC_M_PMM_WPQ_OCCUPANCY.ALL,UNC_M_PMM_RPQ_INSERTS,UNC_M_PMM_WPQ_INSERTS numactl -N ${SOURCE_CPU_NODE} -m ${SOURCE_NODE} ./thp_move_pages ${NUM_PAGES} ${PARAM} ${BATCH} ${SOURCE_NODE} ${DESTINATION_NODE} 2>./thp_verify/${METHOD}_${MT}_2mb_page_order_${N}_${BATCH}_${CONFIG_ID[$pmemOptimized]} | grep -A 3 "\(Total_cycles\|Test successful\)" > ./stats_2mb/${METHOD}_${MT}_page_order_${N}_exchange_${BATCH}_${CONFIG_ID[$pmemOptimized]}
							echo $PERF_EVENT_LIST_STR > ./stats_2mb/${METHOD}_${MT}_page_order_${N}_exchange_${BATCH}_${CONFIG_ID[$pmemOptimized]}_perf_stats
						else
							ocperf stat -x, -o ocperf_temp_output.txt -e UNC_M_PMM_RPQ_OCCUPANCY.ALL,UNC_M_PMM_WPQ_OCCUPANCY.ALL,UNC_M_PMM_RPQ_INSERTS,UNC_M_PMM_WPQ_INSERTS numactl -N ${SOURCE_CPU_NODE} -m ${SOURCE_NODE} ./thp_move_pages ${NUM_PAGES} ${PARAM} ${BATCH} ${SOURCE_NODE} ${DESTINATION_NODE} 2>./thp_verify/${METHOD}_${MT}_2mb_page_order_${N}_${BATCH}_${CONFIG_ID[$pmemOptimized]} | grep -A 3 "\(Total_cycles\|Test successful\)" >> ./stats_2mb/${METHOD}_${MT}_page_order_${N}_exchange_${BATCH}_${CONFIG_ID[$pmemOptimized]}
							echo "" >> ./stats_2mb/${METHOD}_${MT}_page_order_${N}_exchange_${BATCH}_${CONFIG_ID[$pmemOptimized]}_perf_stats
						fi
						for i in `seq 0 4`; do
							line=$(cat ocperf_temp_output.txt | grep -i ${PERF_EVENT_LIST[$i]})
							splittedArr=(${line//,/ })
							currentValue=${splittedArr[0]}
							echo -n "$currentValue," >> ./stats_2mb/${METHOD}_${MT}_page_order_${N}_exchange_${BATCH}_${CONFIG_ID[$pmemOptimized]}_perf_stats
							# sums[$i]=$(echo "print(${sums[$i]}+$currentValue)" | python3)
							# sums[$i]=$((${sums[$i]}+$currentValue))
							# echo ${sums[$i]}
						done

						rm ocperf_temp_output.txt
						sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
						sudo sysctl kernel.enable_nt_exchange_page=0
						sleep 1
					done
				done
			done
		done
	done
done


