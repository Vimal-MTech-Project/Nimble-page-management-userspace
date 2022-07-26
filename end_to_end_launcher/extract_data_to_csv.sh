#!/bin/bash

if [[ "x$1" == "x" ]]; then
	echo "Need to specify a folder"
	exit
fi

FOLDER=$1

BENCHS="503.postencil 504.polbm 553.pclvrleaf 555.pseismic 559.pmniGhost"
CONFIGS="all-local-access rpdaa-nt nt rpdaa nimble-best nimble-default stock-linux all-remote-access"

echo "benchmark_name,migration_mechanism,average_execution_time(ms),execution_time_deviation,migration_bandwidth(MB/GCycles),avg_nvm_media_reads,stddev_nvm_media_reads,avg_nvm_media_writes,stddev_nvm_media_writes"
for B in ${BENCHS}; do
	for C in ${CONFIGS}; do
		if test "$(find ${FOLDER}/*/*GB-${C}-shrink* -name '*cycles' -print -quit 2>/dev/null)"; then
			#echo "${C} "
			#grep "cycles:" ${FOLDER}/${B}/*GB-${C}*/*cycles | cut -f 2 -d" " | awk -v config=${C} '{printf("%s %s\n",config, $0)}'
			AVG=`grep "real time(ms):" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 3 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",sum/n)}'`
			STDDEV=`grep "real time(ms):" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 3 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",stddev)}'`
			# calculate average page migration throughput and latency
			avg_base_pages_exchanged=`grep "ExchangePages.*_nr_base_pages" ${FOLDER}/${B}/*GB-${C}-shrink*/*page_migration_stats_0 | cut -f 2 -d" " | awk '{n += 1; sum += int($1);} END {n/=2; printf("%.2f",sum/n)}'`
			avg_base_pages_migrated=`grep "PageMigrations_nr_base_pages" ${FOLDER}/${B}/*GB-${C}-shrink*/*page_migration_stats_0 | cut -f 2 -d" " | awk '{n += 1; sum += int($1);} END {n/=4; printf("%.2f",sum/n)}'`
			avg_time_taken=`grep "PageMigrations_ttl_time_taken" ${FOLDER}/${B}/*GB-${C}-shrink*/*page_migration_stats_0 | cut -f 2 -d" " | awk '{n += 1; sum += int($1);} END {n/=4; printf("%.2f",sum/n)}'`
			# echo "${B}-${C}: $avg_base_pages_exchanged, $avg_base_pages_migrated, $avg_time_taken"
			migration_bandwidth=$(echo "print(0 if (${avg_time_taken})==0 else (${avg_base_pages_exchanged}*2 + ${avg_base_pages_migrated})/${avg_time_taken}*(1e9)/256)" | python3)
			
			avg_nvm_media_reads=`grep "nvm_media_read_cnt:" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 2 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",sum/n)}'`
			stddev_nvm_media_reads=`grep "nvm_media_read_cnt:" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 2 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",stddev)}'`
			avg_nvm_media_writes=`grep "nvm_media_write_cnt:" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 4 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",sum/n)}'`
			stddev_nvm_media_writes=`grep "nvm_media_write_cnt:" ${FOLDER}/${B}/*GB-${C}-shrink*/*cycles | cut -f 4 -d" " | awk '{n += 1; sum += int($1); sumsq += int($1)^2} END {stddev=sqrt((sumsq - sum^2/n)/n);printf("%.2f",stddev)}'`
			
			echo "${B},${C},${AVG},${STDDEV},${migration_bandwidth},${avg_nvm_media_reads},${stddev_nvm_media_reads},${avg_nvm_media_writes},${stddev_nvm_media_writes}"
		fi
	done
done