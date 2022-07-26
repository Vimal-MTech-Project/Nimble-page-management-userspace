
# construct a data frame having following columns
# benchmark_name, migration_mechanism, execution_time, page_migration_throughput(MB/cycles), nr_pages_migrated

import pandas as pd
import seaborn as sns
import matplotlib as mpl
import matplotlib.pyplot as plt
import os, sys

def main():
    if(len(sys.argv)!=2):
        print("Please provilde exactly one argument representing the csv file containing the result data.\n");
        print("Please use extract_data_to_csv.sh to generate the csv file!\n");
        sys.exit(-1)
    
    df = pd.read_csv(sys.argv[1])
    df["Average Execution Time(sec)"] = df["average_execution_time(ms)"]/1000;
    df["Benchmarks"] = df["benchmark_name"]
    sns.set_style("whitegrid")
    # sns.set_context("paper")
    ax = sns.barplot(x="Benchmarks", y="Average Execution Time(sec)", hue="migration_mechanism", data=df)
    sns.move_legend(ax, "lower center", bbox_to_anchor=(.5, -0.50), ncol=3, title=None, frameon=False)
    plt.tight_layout()
    plt.savefig(sys.argv[1].split(".csv")[0] + "_execution_time.png")
    plt.close()

    df["Average # of reads to NVM media"] = df["avg_nvm_media_reads"]
    ax = sns.barplot(x="Benchmarks", y="Average # of reads to NVM media", hue="migration_mechanism", data=df)
    sns.move_legend(ax, "lower center", bbox_to_anchor=(.5, -0.50), ncol=3, title=None, frameon=False)
    plt.tight_layout()
    plt.savefig(sys.argv[1].split(".csv")[0] + "_nvm_reads.png")
    plt.close()

    df["Average # of writes to NVM media"] = df["avg_nvm_media_writes"]
    ax = sns.barplot(x="Benchmarks", y="Average # of writes to NVM media", hue="migration_mechanism", data=df)
    sns.move_legend(ax, "lower center", bbox_to_anchor=(.5, -0.50), ncol=3, title=None, frameon=False)
    plt.tight_layout()
    plt.savefig(sys.argv[1].split(".csv")[0] + "_nvm_writes.png")
    plt.close()

main()