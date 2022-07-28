#!/bin/bash
sudo sysctl kernel.numa_balancing=1
sudo sysctl kernel.enable_page_migration_optimization_avoid_remote_pmem_write=0
sudo sysctl kernel.enable_nt_exchange_page=0
sudo sysctl kernel.enable_nt_page_copy=0