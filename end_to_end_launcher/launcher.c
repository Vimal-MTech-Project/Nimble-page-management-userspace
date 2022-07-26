/*
 * =====================================================================================
 *
 *       Filename:  time_rdts.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  09/21/2015 05:18:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (),
 *   Organization:
 *
 * =====================================================================================
 */

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#include <errno.h>
#include <limits.h>

#include <nvm_management.h>

#include <time.h>
#include <sys/time.h>
#define TV_MSEC tv_usec / 1000
#include <sys/resource.h>
#include <sys/utsname.h>

#define MPOL_MF_MOVE		(1<<1) /* Trigger page migration operations. */
#define MPOL_MF_MOVE_ALL	(1<<2)
#define MPOL_MF_MOVE_DMA	(1<<5)
#define MPOL_MF_MOVE_MT		(1<<6)
#define MPOL_MF_MOVE_CONCUR	(1<<7)
#define MPOL_MF_EXCHANGE	(1<<8)
#define MPOL_MF_SHRINK_LISTS	(1<<9)

#define MPOL_F_MEMCG	(1<<13)

NVM_UINT64 nvm_media_read_cnt=0, nvm_media_write_cnt=0;

// The following device uids are specific to our experiment setup.
// Please update it as per your setup
// In our setup we use only Optane DIMM region mounted as NUMA node 9
const char* device_uids[] = {
    "8089-a2-2014-00001483", // uid for node 7
    "8089-a2-2014-00001772", // uid for node 8
    "8089-a2-2014-0000202e", // uid for node 9
    "8089-a2-2014-00001a11", // uid for node 10
};

int get_nvm_dimm_cnt(const char* device_uid, NVM_UINT64* bytes_read, NVM_UINT64* bytes_written);


typedef struct
{
  int waitstatus;
  struct rusage ru;
  struct timeval start, elapsed; /* Wallclock time of process.  */
} RESUSE;

/*#define RUSAGE_CHILDREN -1*/

/* Avoid conflicts with ASCII code  */
enum {
	OPT_SLOW_MEM = 256,
	OPT_MEM_MANAGE,
	OPT_MANAGED_PAGES,
	OPT_NON_THP_MIGRATION,
	OPT_THP_MIGRATION,
	OPT_OPT_MIGRATION,
	OPT_EXCHANGE_PAGES,
	OPT_PERF_INTERV,
};

int syscall_mm_manage = 440;

unsigned cycles_high, cycles_low;
unsigned cycles_high1, cycles_low1;
RESUSE time_stats;
pid_t child_pid;
pid_t perf_pid;
pid_t pin_pid;
volatile int child_quit = 0;
volatile int info_done = 0;
int dumpstats_signal = 1;
int dumpstats_period = 1;
int memory_manage = 0;
unsigned long nr_managed_pages = ULONG_MAX;
int vm_stats = 0;
int perf_flamegraph = 0;
volatile int collect_trace_after_second = 0;

static int no_migration = 0;

static struct bitmask *node_mask = NULL;
static struct bitmask *fast_mem_mask = NULL;
static struct bitmask *slow_mem_mask = NULL;

static int use_non_thp_migration = 0;
static int use_thp_migration = 0;
static int use_concur_migration = 0;
static int use_opt_migration = 0;
static int use_basic_exchange_pages = 0;
static int use_concur_only_exchange_pages = 0;
static int use_exchange_pages = 0;
static int shrink_page_lists = 0;
static int prefer_mem_mode = 0;

static int mm_manage_flags = 0;

static int move_hot_and_cold_pages = 0;

static int child_periodic_stats = 1;

static struct sigaction mm_manage_act = {0};

long mm_manage(pid_t pid, unsigned long nr_pages,
		struct bitmask *fromnodes, struct bitmask *tonodes,
		int flags)
{
	int maxnode = numa_num_possible_nodes();
	return syscall(syscall_mm_manage, pid, nr_pages, maxnode + 1,
			fromnodes->maskp, tonodes->maskp, flags);
}

static void sleep_ms(unsigned int milliseconds)
{
	struct timespec ts;

	if (!milliseconds)
		return;

	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

static int get_new_filename(const char *filename, char **final_name)
{
	const char *template = "%s_%d";
	int len = strlen(filename) + 5; /* 1: _, 3: 0-999, 1: \n  */
	int index = 0;
	struct stat st;
	int file_not_exist;

	if (!final_name)
		return -EINVAL;

	*final_name = malloc(len);
	if (!*final_name)
		return -ENOMEM;
	memset(*final_name, 0, len);

	sprintf(*final_name, template,filename, index);

	while ((file_not_exist = stat(*final_name, &st)) == 0)
	{
		index++;
		sprintf(*final_name, template, filename, index);

		if (index >= 1000)
			break;
	}

	if (index >= 1000) {
		free(*final_name);
		*final_name = NULL;
		return -EBUSY;
	}

	return 0;
}

static void mm_manage_handle(int sig)
{
	alarm(dumpstats_period);
	mm_manage(child_pid, nr_managed_pages, slow_mem_mask, fast_mem_mask,
		mm_manage_flags);
}

void read_stats_periodically(pid_t app_pid) {
	/*const char *stats_filename_template = "./mem_frag_stats_%d";*/
	/*const char *vm_stats_filename_template = "./vm_stats_%d";*/
	char proc_buf[64];
	char *stats_filename = NULL;
	char *vm_stats_filename = NULL;
	char *stats_buf = NULL;
	int stats_handle = 0;
	FILE *full_stats_output = NULL;
	FILE *contig_stats_output = NULL;
	FILE *defrag_online_output = NULL;
	FILE *vm_output = NULL;
	FILE *child_stats_output = NULL;
	long read_ret;
	/*int status;*/
	int vm_stats_handle = 0;
	int child_stats_handle = 0;
	/*const int buf_len = 1024 * 1024 * 1024;*/
	const int buf_len = 1024 * 1024 * 16;
	int loop_count = 0;
	
	stats_buf = malloc(buf_len);
	if (!stats_buf)
		return;
	memset(stats_buf, 0, buf_len);

	if (child_periodic_stats) {
		if (get_new_filename("./page_migration_periodic_stats", &stats_filename))
			goto close_and_cleanup;
		child_stats_output = fopen(stats_filename, "w");
		if (!child_stats_output) {
			perror("cannot write child_stats file");
			goto close_and_cleanup;
		}
		sprintf(proc_buf, "/proc/%d/page_migration_stats", app_pid);
		child_stats_handle = open(proc_buf, O_RDONLY);
		if (child_stats_handle < 0) {
			perror("cannot open /proc child_stats");
			child_stats_handle = 0;
			goto close_stats;
		}
	}

	if (vm_stats) {
		if (get_new_filename("./vm_stats", &vm_stats_filename))
			goto close_and_cleanup;

		vm_output = fopen(vm_stats_filename, "w");
		if (!vm_output) {
			perror("cannot write vm_stats file");
			goto close_and_cleanup;
		}

		vm_stats_handle = open("/proc/vmstat", O_RDONLY);
		if (vm_stats_handle < 0) {
			perror("cannot open /proc/vmstat");
			vm_stats_handle = 0;
			goto close_all_and_cleanup;
		}
	}

	if (memory_manage) {
		mm_manage_act.sa_handler = mm_manage_handle;
		if (sigaction(SIGALRM, &mm_manage_act, NULL) < 0) {
			perror("sigaction on memory_manage");
			exit(0);
		}

		if (dumpstats_signal)
			alarm(dumpstats_period);
	}

	sleep(1);
	do {
		if (dumpstats_signal) {
			loop_count++;


			if (child_periodic_stats) {
				lseek(child_stats_handle, 0, SEEK_SET);
				while ((read_ret = read(child_stats_handle, stats_buf, buf_len)) > 0) {
					fputs(stats_buf, child_stats_output);
					memset(stats_buf, 0, buf_len);
				}
				if (read_ret < 0)
					break;
				fputs("----\n", child_stats_output);
			}

			if (vm_stats) {
				lseek(vm_stats_handle, 0, SEEK_SET);
				while ((read_ret = read(vm_stats_handle, stats_buf, buf_len)) > 0) {
					fputs(stats_buf, vm_output);
					memset(stats_buf, 0, buf_len);
				}
				if (read_ret < 0)
					break;
				fputs("----\n", vm_output);
			}
		}
		sleep(dumpstats_period);
	} while (!child_quit);

	if (vm_stats_handle)
		close(vm_stats_handle);
close_all_and_cleanup:
	if (vm_output)
		fclose(vm_output);
	if (child_stats_handle)
		close(child_stats_handle);
close_stats:
	if (child_stats_output)
		fclose(child_stats_output);
close_and_cleanup:
	if (stats_buf)
		free(stats_buf);
	if (stats_filename)
		free(stats_filename);
	if (vm_stats_filename)
		free(vm_stats_filename);

	return;
}

void toggle_dumpstats_signal()
{
	dumpstats_signal ^= 1;
}

void child_exit(int sig, siginfo_t *siginfo, void *context)
{
	char buffer[255];
	char proc_buf[64];
    uint64_t start;
    uint64_t end;
	int status;
	unsigned long r;		/* Elapsed real milliseconds.  */
	unsigned long system_time;
	unsigned long user_time;
	FILE *childinfo = NULL;
	char *stats_filename = NULL;
	int child_stats_handle = 0;
	int read_ret;
	unsigned long cpu_freq = 1;
	char *hz;
	char *unit;

	if (waitpid(siginfo->si_pid, &status, WNOHANG) != child_pid)
		return;

	child_quit = 1;
	getrusage(RUSAGE_CHILDREN, &time_stats.ru);
    asm volatile
        ( "RDTSCP\n\t"
          "mov %%edx, %0\n\t"
          "mov %%eax, %1\n\t"
          "CPUID\n\t"
          :
          "=r" (cycles_high1), "=r" (cycles_low1)
          ::
          "rax", "rbx", "rcx", "rdx"
        );
	gettimeofday (&time_stats.elapsed, (struct timezone *) 0);

	time_stats.elapsed.tv_sec -= time_stats.start.tv_sec;
	if (time_stats.elapsed.tv_usec < time_stats.start.tv_usec)
	{
		/* Manually carry a one from the seconds field.  */
		time_stats.elapsed.tv_usec += 1000000;
		--time_stats.elapsed.tv_sec;
	}
	time_stats.elapsed.tv_usec -= time_stats.start.tv_usec;

	time_stats.waitstatus = status;

	r = time_stats.elapsed.tv_sec * 1000 + time_stats.elapsed.tv_usec / 1000;

	user_time = time_stats.ru.ru_utime.tv_sec * 1000 + time_stats.ru.ru_utime.TV_MSEC;
	system_time = time_stats.ru.ru_stime.tv_sec * 1000 + time_stats.ru.ru_stime.TV_MSEC;


    start = ((uint64_t)cycles_high <<32 | cycles_low);
    end = ((uint64_t)cycles_high1 <<32 | cycles_low1);


	fprintf(stderr, "cycles: %lu\n", end - start);

	fprintf(stderr, "real time(ms): %lu, user time(ms): %lu, system time(ms): %lu, virtual cpu time(ms): %lu\n",
			r, user_time, system_time, user_time+system_time);
	fprintf(stderr, "min_flt: %lu, maj_flt: %lu, maxrss: %lu KB\n",
			time_stats.ru.ru_minflt, time_stats.ru.ru_majflt, 
			time_stats.ru.ru_maxrss);
	
	NVM_UINT64 current_nvm_media_read_cnt, current_nvm_media_write_cnt;
	get_nvm_dimm_cnt(device_uids[2], &current_nvm_media_read_cnt, &current_nvm_media_write_cnt);
	nvm_media_read_cnt = current_nvm_media_read_cnt - nvm_media_read_cnt;
	nvm_media_write_cnt = current_nvm_media_write_cnt - nvm_media_write_cnt;
	fprintf(stderr, "nvm_media_read_cnt: %llu, nvm_media_write_cnt: %llu\n", nvm_media_read_cnt, nvm_media_write_cnt);

	fflush(stderr);

	if (get_new_filename("./page_migration_stats", &stats_filename))
		goto close_and_cleanup;
	childinfo = fopen(stats_filename, "w");
	if (!childinfo) {
		perror("cannot write child_stats file");
		goto close_and_cleanup;
	}
	sprintf(proc_buf, "/proc/%d/child_stats", getpid());
	child_stats_handle = open(proc_buf, O_RDONLY);
	if (child_stats_handle < 0) {
		perror("cannot open /proc child_stats");
		child_stats_handle = 0;
		goto close_and_cleanup;
	}
	while ((read_ret = read(child_stats_handle, buffer, 255)) > 0) {
		fputs(buffer, childinfo);
		memset(buffer, 0, 255);
	}

	fflush(childinfo);

close_and_cleanup:
	if (stats_filename)
		free(stats_filename);
	if (childinfo)
		fclose(childinfo);
	if (child_stats_handle)
		close(child_stats_handle);

	if (perf_pid)
		kill(perf_pid, SIGINT);

	info_done = 1;
}

int main(int argc, char** argv)
{
	static int dumpstats = 0;
	static int use_dumpstats_signal = 0;
	static struct option long_options [] = 
	{
		{"cpunode", required_argument, 0, 'N'},
		{"fast_mem", required_argument, 0, 'm'},
		{"slow_mem", required_argument, 0, OPT_SLOW_MEM},
		{"prefer_memnode", no_argument, &prefer_mem_mode, 1},
		{"cpumask", required_argument, 0, 'c'},
		{"dumpstats", no_argument, &dumpstats, 1},
		{"dumpstats_signal", no_argument, &use_dumpstats_signal, 1},
		{"dumpstats_period", required_argument, 0, 'p'},
		{"memcg", required_argument, 0, 'g'},
		{"nomigration", no_argument, &no_migration, 1},
		{"memory_manage", no_argument, &memory_manage, 1},
		{"managed_pages", required_argument, 0, OPT_MANAGED_PAGES},

		{"non_thp_migration", no_argument, &use_non_thp_migration, 1},
		{"thp_migration", no_argument, &use_thp_migration, 1},
		{"concur_migration", no_argument, &use_concur_migration, 1},
		{"opt_migration", no_argument, &use_opt_migration, 1},
		{"basic_exchange_pages", no_argument, &use_basic_exchange_pages, 1},
		{"concur_only_exchange_pages", no_argument, &use_concur_only_exchange_pages, 1},
		{"exchange_pages", no_argument, &use_exchange_pages, 1},
		{"move_hot_and_cold_pages", no_argument, &move_hot_and_cold_pages, 1},
		{"shrink_page_lists", no_argument, &shrink_page_lists, 1},

		{"vm_stats", no_argument, &vm_stats, 1},
		{"perf_loc", required_argument, 0, 'l'},
		{"perf_events", required_argument, 0, 'P'},
		{"perf_flamegraph", no_argument, &perf_flamegraph, 1},
		{"perf_interv", required_argument, 0, OPT_PERF_INTERV},
		{"child_stdin", required_argument, 0, 'i'},
		{0,0,0,0}
	};
	struct sigaction child_exit_act = {0}, dumpstats_act = {0};

	int option_index = 0;
	int c;
	unsigned long cpumask = -1;
	int index;
	struct bitmask *cpu_mask = NULL;
	struct bitmask *parent_mask = NULL;
	char memcg_proc[256] = {0};
	int use_memcg = 0;
	char perf_events[512] = {0};
	char perf_loc[256] = {0};
	int perf_interv = 0;
	int use_perf = 0;
	int child_stdin_fd = 0;
	struct utsname kernel_info;

	/* get kernel version at runtime, change syscall number accordingly  */
	if (uname(&kernel_info))
		return 0;

	parent_mask = numa_allocate_nodemask();

	if (!parent_mask)
		numa_error("numa_allocate_nodemask");

	numa_bitmask_setbit(parent_mask, 1);

	/*numa_run_on_node(0);*/
	numa_bind(parent_mask);

	if (argc < 2)
		return 0;

	while ((c = getopt_long(argc, argv, "N:M:m:c:",
							long_options, &option_index)) != -1) 
	{
		switch (c)
		{
			case 0:
				 /* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				printf ("option %s", long_options[option_index].name);
				if (optarg)
					printf (" with arg %s", optarg);
					printf ("\n");
				break;
			case 'N':
				/* cpunode = (int)strtol(optarg, NULL, 0); */
				node_mask = numa_parse_nodestring(optarg);
				break;
			case 'm':
				/* memnode = (int)strtol(optarg, NULL, 0); */
				fast_mem_mask = numa_parse_nodestring(optarg);
				break;
			case OPT_SLOW_MEM:
				slow_mem_mask = numa_parse_nodestring(optarg);
				break;
			case 'c':
				cpumask = strtoul(optarg, NULL, 0);
				cpu_mask = numa_allocate_nodemask();
				index = 0;
				while (cpumask) {
					if (cpumask & 1) {
						numa_bitmask_setbit(cpu_mask, index);
					}
					cpumask = cpumask >> 1;
					++index;
				}
				break;
			case OPT_MANAGED_PAGES:
				nr_managed_pages = atol(optarg);
				break;
			case 'g':
				strncpy(memcg_proc, optarg, 255);
				use_memcg = 1;
				break;
			case 'p':
				dumpstats_period = atoi(optarg);
				break;
			case 'P':
				strncpy(perf_events, optarg, 512);
				break;
			case 'l':
				strncpy(perf_loc, optarg, 255);
				use_perf = 1;
				break;
			case 'i':
				child_stdin_fd = open(optarg, O_RDONLY);
				if (!child_stdin_fd) {
					perror("child stdin file open error\n");
					exit(-1);
				}
				break;
			case OPT_PERF_INTERV:
				perf_interv = atoi(optarg);
				break;
			case '?':
				return 1;
			default:
				abort();
		}
	}


	/* push it to child process command line  */
	argv += optind;

	printf("child arg: %s\n", argv[0]);

	if (memory_manage && !(fast_mem_mask && slow_mem_mask)) {
		fprintf(stderr, "Both fast and slow memory need to be specified to run memory manage\n");
		exit(-1);
	}
	/*printf("fast_mem_mask weight: %u, slow_mem_mask weight: %u\n",*/
			/*numa_bitmask_weight(fast_mem_mask),*/
			/*numa_bitmask_weight(slow_mem_mask));*/

	if (use_non_thp_migration + use_thp_migration + use_concur_migration +
		use_opt_migration + use_basic_exchange_pages + use_concur_only_exchange_pages +
		use_exchange_pages > 1) {
		fprintf(stderr, "Only one page migration method can be used at a time\n");
		exit(-1);
	}

	if (use_non_thp_migration || use_thp_migration)
		mm_manage_flags |= MPOL_MF_MOVE;
	if (use_concur_migration)
		mm_manage_flags |= MPOL_MF_MOVE|MPOL_MF_MOVE_CONCUR;
	if (use_opt_migration)
		mm_manage_flags |= MPOL_MF_MOVE|MPOL_MF_MOVE_MT|MPOL_MF_MOVE_CONCUR;
	if (use_basic_exchange_pages)
		mm_manage_flags |= MPOL_MF_MOVE|MPOL_MF_EXCHANGE;
	if (use_concur_only_exchange_pages)
		mm_manage_flags |= MPOL_MF_MOVE|MPOL_MF_MOVE_CONCUR|MPOL_MF_EXCHANGE;
	if (use_exchange_pages)
		mm_manage_flags |= MPOL_MF_MOVE|MPOL_MF_MOVE_MT|MPOL_MF_MOVE_CONCUR|MPOL_MF_EXCHANGE;

	if (shrink_page_lists)
		mm_manage_flags |= MPOL_MF_SHRINK_LISTS;
	if (move_hot_and_cold_pages)
		mm_manage_flags |= MPOL_MF_MOVE_ALL;
	if (no_migration)
		mm_manage_flags &= ~MPOL_MF_MOVE;

	/* cpu_mask overwrites node_mask  */
	if (cpu_mask)
	{
		numa_bitmask_free(node_mask);
		node_mask = NULL;
	}

	child_exit_act.sa_sigaction = child_exit;
	child_exit_act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGCHLD, &child_exit_act, NULL) < 0) {
		perror("sigaction on SIGCHLD");
		exit(0);
	}

	dumpstats_act.sa_handler = toggle_dumpstats_signal;
	if (sigaction(SIGUSR1, &dumpstats_act, NULL) < 0) {
		perror("sigaction on dumpstats");
		exit(0);
	}

    asm volatile
        ( "CPUID\n\t"
          "RDTSC\n\t"
          "mov %%edx, %0\n\t"
          "mov %%eax, %1\n\t"
          :
          "=r" (cycles_high), "=r" (cycles_low)
          ::
          "rax", "rbx", "rcx", "rdx"
        );
	gettimeofday (&time_stats.start, (struct timezone *) 0);

	// read current read/write counters for nvm media
	get_nvm_dimm_cnt(device_uids[2], &nvm_media_read_cnt, &nvm_media_write_cnt);

	child_pid = fork();

	if (child_pid == 0) { // child
		int child_status;

		if (use_memcg) {
			int memcgd = open(memcg_proc, O_RDWR);
			char mypid[10];
			int err;

			if (memcgd < 0) {
				fprintf(stderr, "cannot open the memcg\n");
				exit(0);
			}

			sprintf(mypid, "%d\n", getpid());
			if ((err = write(memcgd, mypid, sizeof(mypid))) <= 0) {
				fprintf(stderr, "write to  memcg: %s error: %d\n", memcg_proc, err);
				if (err < 0)
					perror("memcg error");
				exit(0);
			}
			close(memcgd);
		}

		if (node_mask)
		{
			if (numa_run_on_node_mask_all(node_mask) < 0)
				numa_error("numa_run_on_node_mask_all");
		} else if (cpu_mask) {
			if (sched_setaffinity(getpid(), numa_bitmask_nbytes(cpu_mask), 
							(cpu_set_t*)cpu_mask->maskp) < 0)
				numa_error("sched_setaffinity");
		}

		if (slow_mem_mask) {
			if (prefer_mem_mode) {
				if (set_mempolicy(MPOL_PREFERRED|MPOL_F_MEMCG,
							  fast_mem_mask->maskp,
							  fast_mem_mask->size + 1) < 0)
					numa_error("set_mempolicy: preferred");
			} else {
				if (set_mempolicy(MPOL_BIND,
							  slow_mem_mask->maskp,
							  slow_mem_mask->size + 1) < 0)
					numa_error("set_mempolicy: mbind");
			}
		}

		if (child_stdin_fd)
			dup2(child_stdin_fd, 0);

		child_status = execvp(argv[0], argv);

		perror("child die\n");
		fprintf(stderr, "application execution error: %d\n", child_status);
		exit(-1);
	}

	fprintf(stderr, "child pid: %d\n", child_pid);
	fprintf(stdout, "child pid: %d\n", child_pid);

	if (use_perf) {
		char child_pid_str[8] = {0};

		/*sprintf(perf_cmd, "/gauls/kernels/linux/tools/perf/perf stat -e %s -p %d -o perf_results", perf_events, child);*/
		sprintf(child_pid_str, "%d", child_pid);

		perf_pid = fork();
		if (perf_pid == 0) {
			if (perf_flamegraph) {
				if (strlen(perf_loc))
					execl(perf_loc, "perf", "record",
						  "-F", "99",
						  "-g",
						  "-p", child_pid_str,
						  "-o", "perf_results", (char *)NULL);
				else
					execl("perf", "perf", "record",
						  "-F", "99",
						  "-g",
						  "-p", child_pid_str,
						  "-o", "perf_results", (char *)NULL);
			} else {
				if (perf_interv) {
					char interv[8] = {0};

					sprintf(interv, "%d", perf_interv);
					if (strlen(perf_loc))
						execl(perf_loc, "perf", "stat",
							  "-e", perf_events, "-p", child_pid_str,
							  "-I", interv,
							  "-o", "perf_results", (char *)NULL);
					else
						execl("perf", "perf", "stat",
							  "-e", perf_events, "-p", child_pid_str,
							  "-I", interv,
							  "-o", "perf_results", (char *)NULL);
				} else {
					if (strlen(perf_loc))
						execl(perf_loc, "perf", "stat",
							  "-e", perf_events, "-p", child_pid_str,
							  "-o", "perf_results", (char *)NULL);
					else
						execl("perf", "perf", "stat",
							  "-e", perf_events, "-p", child_pid_str,
							  "-o", "perf_results", (char *)NULL);
				}
			}

			perror("perf execution error\n");
			exit(-1);
		}
	}



	if (use_dumpstats_signal)
		dumpstats_signal = 0;

	if (dumpstats || use_dumpstats_signal)
		read_stats_periodically(child_pid);


	if (use_perf) {
		int status;
		waitpid(perf_pid, &status, 0);
	}

	while (!info_done)
		sleep(1);

	if (slow_mem_mask)
		numa_bitmask_free(slow_mem_mask);
	if (fast_mem_mask)
		numa_bitmask_free(fast_mem_mask);
	if (node_mask)
		numa_bitmask_free(node_mask);
	if (cpu_mask)
		numa_bitmask_free(cpu_mask);
	if (parent_mask)
		numa_bitmask_free(parent_mask);

	

	return 0;

}


/**
  Get device discovery structure and dimm count

  @param[out] p_dev_count The dimm count
  @param[out] p_devices device discovery structure for each dimm

  @retval -1 Failure
  @retval 0  Success
**/
int get_devices(unsigned int *p_dev_count, struct device_discovery** p_devices)
{
  int nvm_return = 0;
  if (NVM_SUCCESS != (nvm_return = nvm_get_number_of_devices(p_dev_count)))
  {
    printf("nvm_get_number_of_devices failed: %d.\n", nvm_return);
    return -1;
  }

  if (NULL == (*p_devices = (struct device_discovery*)malloc(sizeof(struct device_discovery) * *p_dev_count)))
  {
    printf("failed to allocate device discovery structure.\n");
    return -1;
  }

  if (NVM_SUCCESS != (nvm_return = nvm_get_devices(*p_devices, *p_dev_count)))
  {
    printf("nvm_get_number_of_devices failed: %d.\n", nvm_return);
    free(*p_devices);
    *p_devices = NULL;
    return -1;
  }

  return 0;
}

int get_nvm_device_details(const char *device_uid, struct device_details *p_details){
    int nvm_return = 0;
    struct device_discovery* p_devices = NULL;
    unsigned int Index = 0;
    unsigned int p_dev_count;
    nvm_return = get_devices(&p_dev_count, &p_devices);

    if (nvm_return != 0)
    {
        printf("get_devices failed: %d.\n", nvm_return);
        return -1;
    }
    
    if (NVM_SUCCESS != (nvm_return = nvm_get_device_details(device_uid, p_details))) {
        printf("failed to get device details.\n");
        free(p_devices);
        return -1;
    }

    free(p_devices);
    return 0;
}

// reads NVM read/write counter for Optane DIMM with uid device_uid and stores them in location 
// bytes_read and bytes_written
int get_nvm_dimm_cnt(const char* device_uid, unsigned long long int* bytes_read, unsigned long long int* bytes_written){
    nvm_init();
    struct device_details p_details;
    int ret = 0;
    
    if(get_nvm_device_details(device_uid, &p_details)){
        ret = -1;
        goto out;
    }

    *bytes_read = p_details.performance.bytes_read;
    *bytes_written = p_details.performance.bytes_written;

    // printf("read_cnt=%llx, write_cnt=%llx\n", p_details.performance.bytes_read, p_details.performance.bytes_written);
out:
    return ret;
}