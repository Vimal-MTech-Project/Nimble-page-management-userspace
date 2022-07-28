/*
 * Test program to test the moving of a processes pages.
 *
 * From:
 * http://numactl.sourcearchive.com/documentation/2.0.2/migrate__pages_8c-source.html
 *
 * (C) 2006 Silicon Graphics, Inc.
 *          Christoph Lameter <clameter@sgi.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include "numa.h"
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <chrono>

int SOURCE_NUMA_NODE = 2;
int DESTINATION_NUMA_NODE = 3;

unsigned int pagesize;
unsigned int page_count = 32;

char *page_base;
char *pages;

void **addr;
int *status;
int *nodes;
int *src_nodes;
int errors;
int nr_nodes;

struct bitmask *old_nodes;
struct bitmask *new_nodes;

#define PAGE_4K (4UL * 1024)
#define PAGE_2M (PAGE_4K * 512)
/*#define PAGE_1G (PAGE_2M*512)*/
#define PAGE_1G (128 * 1024 * 1024)

#define PRESENT_MASK (1UL << 63)
#define SWAPPED_MASK (1UL << 62)
#define PAGE_TYPE_MASK (1UL << 61)
#define PFN_MASK ((1UL << 55) - 1)

#define KPF_THP (1UL << 22)

void print_paddr_and_flags(char *bigmem, int pagemap_file, int kpageflags_file)
{
	uint64_t paddr;
	uint64_t page_flags;

	if (pagemap_file)
	{
		pread(pagemap_file, &paddr, sizeof(paddr), ((long)bigmem >> 12) * sizeof(paddr));

		if (kpageflags_file)
		{
			pread(kpageflags_file, &page_flags, sizeof(page_flags),
				  (paddr & PFN_MASK) * sizeof(page_flags));

			fprintf(stderr, "vpn: 0x%lx, pfn: 0x%lx is %s %s, %s, %s\n",
					((long)bigmem) >> 12,
					(paddr & PFN_MASK),
					paddr & PAGE_TYPE_MASK ? "file-page" : "anon",
					paddr & PRESENT_MASK ? "there" : "not there",
					paddr & SWAPPED_MASK ? "swapped" : "not swapped",
					page_flags & KPF_THP ? "thp" : "not thp"
					/*page_flags*/
			);
		}
	}
}

int main(int argc, char **argv)
{
	int i, rc;
	unsigned long begin = 0, end = 0;
	unsigned cycles_high, cycles_low;
	unsigned cycles_high1, cycles_low1;
	const char *move_pages_stats = "/proc/%d/move_pages_breakdown";
	const char *pagemap_template = "/proc/%d/pagemap";
	const char *kpageflags_proc = "/proc/kpageflags";
	char move_pages_stats_proc[255];
	char pagemap_proc[255];
	char stats_buffer[1024] = {0};
	char transfer_method[255] = {0};
	int stats_fd;
	int pagemap_fd;
	int kpageflags_fd;
	int pmemOptimized = 0;

	/*pagesize = getpagesize();*/
	pagesize = PAGE_4K;

	nr_nodes = numa_max_node() + 1;

	if (argc > 1)
		sscanf(argv[1], "%d", &page_count);
	if (argc > 2)
		sscanf(argv[2], "%s", transfer_method);
	if (argc > 3)
		sscanf(argv[3], "%d", &SOURCE_NUMA_NODE);
	if (argc > 4)
		sscanf(argv[4], "%d", &DESTINATION_NUMA_NODE);
	if (argc > 5)
		sscanf(argv[5], "%d", &pmemOptimized);

	old_nodes = numa_bitmask_alloc(nr_nodes);
	new_nodes = numa_bitmask_alloc(nr_nodes);
	numa_bitmask_setbit(old_nodes, DESTINATION_NUMA_NODE);
	numa_bitmask_setbit(new_nodes, SOURCE_NUMA_NODE);

	if (nr_nodes < 2)
	{
		printf("A minimum of 2 nodes is required for this test.\n");
		exit(1);
	}

	setbuf(stdout, NULL);
	printf("migrate_pages() test ......\n");

	if (strncmp(transfer_method, "dma", 3) == 0)
	{
		printf("-----Using DMA-----\n");
	}
	if (strncmp(transfer_method, "mt", 2) == 0)
	{
		printf("-----Using Multi Threads-----\n");
	}

	/*page_base = malloc((pagesize ) * page_count);*/
	page_base = (char *)aligned_alloc(pagesize, pagesize * page_count);
	addr = (void **)malloc(sizeof(char *) * page_count);
	status = (int *)malloc(sizeof(int *) * page_count);
	nodes = (int *)malloc(sizeof(int *) * page_count);
	src_nodes = (int *)malloc(sizeof(int *) * page_count);
	if (!page_base || !addr || !status || !nodes)
	{
		printf("Unable to allocate memory\n");
		exit(1);
	}

	madvise(page_base, pagesize * page_count, MADV_NOHUGEPAGE);
	/*pages = (void *) ((((long)page_base) & ~((long)(pagesize - 1))) + pagesize);*/

	pages = page_base;

	for (i = 0; i < page_count; i++)
	{
		pages[i * pagesize] = (char)i;
		addr[i] = pages + i * pagesize;
		nodes[i] = DESTINATION_NUMA_NODE;
		src_nodes[i] = SOURCE_NUMA_NODE;
		status[i] = -123;
	}

	sprintf(pagemap_proc, pagemap_template, getpid());
	pagemap_fd = open(pagemap_proc, O_RDONLY);

	if (pagemap_fd == -1)
	{
		perror("read pagemap:");
		exit(-1);
	}

	kpageflags_fd = open(kpageflags_proc, O_RDONLY);

	if (kpageflags_fd == -1)
	{
		perror("read kpageflags:");
		exit(-1);
	}

	for (i = 0; i < page_count; ++i)
	{
		print_paddr_and_flags(pages + pagesize * i, pagemap_fd, kpageflags_fd);
	}
	sprintf(move_pages_stats_proc, move_pages_stats, getpid());
	stats_fd = open(move_pages_stats_proc, O_RDONLY);

	if (stats_fd == -1)
	{
		perror("read stats:");
		exit(-1);
	}

	asm volatile("CPUID\n\t"
				 "RDTSC\n\t"
				 "mov %%edx, %0\n\t"
				 "mov %%eax, %1\n\t"
				 : "=r"(cycles_high), "=r"(cycles_low)::"rax", "rbx", "rcx", "rdx");
	begin = ((uint64_t)cycles_high << 32 | cycles_low);
	auto start_time = std::chrono::system_clock::now();

	/* Move to starting node */
	if (strncmp(transfer_method, "dma", 3) == 0)
		rc = numa_move_pages(0, page_count, addr, nodes, status, (1 << 5));
	else if (strncmp(transfer_method, "mt", 2) == 0)
		rc = numa_move_pages(0, page_count, addr, nodes, status, (1 << 6));
	else
		rc = numa_move_pages(0, page_count, addr, nodes, status, 0);

	if (rc < 0 && errno != ENOENT)
	{
		perror("move_pages");
		exit(1);
	}

	if (strncmp(transfer_method, "dma", 3) == 0)
		rc = numa_move_pages(0, page_count, addr, src_nodes, status, (1 << 5));
	else if (strncmp(transfer_method, "mt", 2) == 0)
		rc = numa_move_pages(0, page_count, addr, src_nodes, status, (1 << 6));
	else
		rc = numa_move_pages(0, page_count, addr, src_nodes, status, 0);

	if (rc < 0 && errno != ENOENT)
	{
		perror("move_pages");
		exit(1);
	}

	asm volatile("RDTSCP\n\t"
				 "mov %%edx, %0\n\t"
				 "mov %%eax, %1\n\t"
				 "CPUID\n\t"
				 : "=r"(cycles_high1), "=r"(cycles_low1)::"rax", "rbx", "rcx", "rdx");

	end = ((uint64_t)cycles_high1 << 32 | cycles_low1);
	auto end_time = std::chrono::system_clock::now();

	printf("+++++After moved to node 1+++++\n");
	for (i = 0; i < page_count; ++i)
	{
		print_paddr_and_flags(pages + pagesize * i, pagemap_fd, kpageflags_fd);
	}

	pread(stats_fd, stats_buffer, sizeof(stats_buffer), 0);

	printf("Total_nanoseconds\tTotal_cycles\tBegin_timestamp\tEnd_timestamp\n"
		   "%llu\t%llu\t%llu\t%llu\n",
		   std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count(),
		   (end - begin), begin, end);
	printf("%s", stats_buffer);

	// Verify the location of the page after migration
	numa_move_pages(0, page_count, addr, NULL, status, 0);
	for (i = 0; i < page_count; i++)
	{
		/*printf("Page %d vaddr=%p node=%d\n", i, pages + i * pagesize, status[i]);*/
		if (status[i] != SOURCE_NUMA_NODE)
		{
			fprintf(stderr, "Bad page state before migrate_pages. Page %d status %d\n", i, status[i]);
			exit(1);
		}
	}

	if (!errors)
		printf("Test successful.\n");
	else
		fprintf(stderr, "%d errors.\n", errors);

	/*close(stats_fd);*/
	close(pagemap_fd);
	close(kpageflags_fd);

	return errors > 0 ? 1 : 0;
}
