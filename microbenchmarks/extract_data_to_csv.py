from stats import stats
import matplotlib.pyplot as plt
import seaborn as sns
from matplotlib.pyplot import figure
import matplotlib as mpl
import math
import csv
import pandas as pd
import os.path

def main():
    base_dir = "."
    file_name_templates = [
            base_dir + "/thp_page_migration_and_parallel/stats_thp/mt_{0}_2mb_page_order_{1}_{2}_NT_{3}_RPDAA_{4}",
            base_dir + "/thp_page_migration_and_parallel/stats_split_thp/mt_{0}_split_thp_2mb_page_order_{1}_{2}_NT_{3}_RPDAA_{4}",            
            base_dir + "/exchange_page_migration/stats_2mb/mt_{0}_page_order_{1}_exchange_batch_{2}_NT_{3}_RPDAA_{4}",
            base_dir + "/exchange_page_migration/stats_2mb/seq_{0}_page_order_{1}_exchange_batch_{2}_NT_{3}_RPDAA_{4}"
    ]
    migration_mechanism_name = [
        "native THP",
        "split THP",
        "exchange THP",
        "exchange THP",
    ]
    thread_counts=[1,2,4]
    page_orders = [i for i in range(0, 10)]

    df = pd.DataFrame(columns=["configuration", "migration_mechanism", "thread_cnt", "page_cnt", "rpdaa", "nt", "bandwidth(MBps)"])
    row_cnt = 0
    # construct a data frame
    for file_name_template_id in range(len(file_name_templates)):
        for thread_cnt in thread_counts:
            for page_order in page_orders:
                for mem_config in ["dram_to_dram", "dram_to_pmem", "pmem_to_dram", "pmem_to_pmem"]:
                    for nt_config in [True, False]:
                        for rpdaa_config in [True, False]:
                            file_name = file_name_templates[file_name_template_id].\
                                format(thread_cnt, page_order, mem_config, "ON" if nt_config else "OFF", "ON" if rpdaa_config else "OFF")
                            if(not os.path.isfile(file_name)):
                                continue
                            stats_obj = stats(file_name)
                            total_migrated_MBytes = (1<<page_order)*(1<<21)/(1<<20)
                            if(migration_mechanism_name[file_name_template_id]=="exchange THP"):
                                total_migrated_MBytes *= 2
                            total_seconds = stats_obj.average_stats["Total_nanoseconds"]/(1e9)
                            bandwidth = (total_migrated_MBytes/total_seconds)
                            df.loc[row_cnt] = [mem_config.replace("_", " "), migration_mechanism_name[file_name_template_id], thread_cnt, 1<<page_order, rpdaa_config, nt_config, bandwidth]
                            row_cnt+=1
                # for pmem_optimized in ["pmem_optimized", "not_pmem_optimized"]:
    df.to_csv("summarized_microbench_results.csv")

main()