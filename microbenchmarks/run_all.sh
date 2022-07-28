#!/bin/bash

# TODO 1:
# We require two DRAM nodes on different sockets
# We also require two PMEM NUMA nodes, each on a different sockets
# Make sure that DRAM_NODE_1 should not be on a same socket as DRAM_NODE_2 or PMEM_NODE_2
# and PMEM_NODE_1 should not be on a same socket as DRAM_NODE_2 or PMEM_NODE_2
# Then, update the following four variables as per your setup
DRAM_NODE_1=2
DRAM_NODE_2=3
PMEM_NODE_1=7
PMEM_NODE_2=9

# TODO 2: 
# Download pmu-tools from https://github.com/andikleen/pmu-tools 
# and set PMU_TOOLS_DIR appropriately
PMU_TOOLS_DIR=/home/vimal/pmu-tools
export PATH=$PATH:$PMU_TOOLS_DIR

SOURCE_NODES=($DRAM_NODE_1 $DRAM_NODE_1 $PMEM_NODE_1 $PMEM_NODE_1)
SOURCE_CPU_NODES=($DRAM_NODE_1 $DRAM_NODE_1 $DRAM_NODE_1 $DRAM_NODE_1)
DESTINATION_NODES=($DRAM_NODE_2 $PMEM_NODE_2 $DRAM_NODE_2 $PMEM_NODE_2)
CONFIGURATION_NAMES=("dram_to_dram" "dram_to_pmem" "pmem_to_dram" "pmem_to_pmem")

ACTIVE_CONFIGS=(0 1 2 3)

# turnoff autonuma
sudo sysctl kernel.numa_balancing=0

trap "./cleanup.sh; exit" INT

for i in ${ACTIVE_CONFIGS[@]}; do
    echo "========================= >>> Executing Configuration: ${CONFIGURATION_NAMES[$i]} <<< ======================"
    export SOURCE_NODE=${SOURCE_NODES[$i]}
    export SOURCE_CPU_NODE=${SOURCE_CPU_NODES[$i]}
    export DESTINATION_NODE=${DESTINATION_NODES[$i]}
    export CONFIGURATION_NAME=${CONFIGURATION_NAMES[$i]}

    # run concurrent page migration benchmarks
    cd concurrent_page_migration;
    make non_thp_move_pages;
    make thp_move_pages;
    echo "============executing concurrent_page_migration/run_non_thp_test.sh================"
    ./run_non_thp_test.sh
    echo "============executing concurrent_page_migration/run_thp_test.sh===================="
    ./run_thp_test.sh
    cd ..

    # run exchange page migration benchmark
    cd exchange_page_migration
    make non_thp_move_pages;
    make thp_move_pages;
    echo "============executing exchange_page_migration/run_non_thp_test.sh================"
    ./run_non_thp_test.sh
    echo "============executing exchange_page_migration/run_thp_test.sh===================="
    ./run_thp_test.sh
    cd ..

    # run parallel and native thp page migration benchmark
    cd thp_page_migration_and_parallel
    make non_thp_move_pages;
    make thp_move_pages;
    echo "============executing thp_page_migration_and_parallel/run_non_thp_test.sh================"
    ./run_non_thp_test.sh
    echo "============executing thp_page_migration_and_parallel/run_non_thp_2mb_page_test.sh================"
    ./run_non_thp_2mb_page_test.sh
    echo "============executing thp_page_migration_and_parallel/run_split_thp_test.sh================"
    ./run_split_thp_test.sh
    echo "============executing thp_page_migration_and_parallel/run_thp_test.sh================"
    ./run_thp_test.sh
    cd ..
done

sudo sysctl kernel.numa_balancing=1
