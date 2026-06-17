/*
 * Phase 2 test: load a binary into L2 via mmap, boot the PULP cluster,
 * wait for EOC, and print the return value.
 *
 * Usage:
 *   ./cluster_test <binary.bin>
 *
 * <binary.bin> is the stripped ELF / raw binary of pulp_hello compiled
 * with pulp-runtime. Without FPGA hardware this test will fail at mmap().
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "../driver/carfield.h"

#define L2_SIZE  0x100000   /* 1 MB */
#define L2_PHYS  0x78000000 /* physical base of L2_INTL_0 */

int main(int argc, char *argv[])
{
	int fd, bin_fd;
	void *l2;
	struct stat st;
	void *bin_buf;
	struct carfield_cluster_run req;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <binary.bin>\n", argv[0]);
		return 1;
	}

	/* Open Carfield device */
	fd = open("/dev/carfield", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open /dev/carfield");
		return 1;
	}

	/* Map L2 interleaved bank 0 into user space */
	l2 = mmap(NULL, L2_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		  fd, CARFIELD_MMAP_L2_INTL_0 * getpagesize());
	if (l2 == MAP_FAILED) {
		perror("mmap L2 (requires FPGA hardware)");
		close(fd);
		return 1;
	}

	/* Read binary file */
	bin_fd = open(argv[1], O_RDONLY);
	if (bin_fd < 0) {
		perror("open binary");
		goto out_unmap;
	}
	fstat(bin_fd, &st);
	if ((size_t)st.st_size > L2_SIZE) {
		fprintf(stderr, "binary too large for L2\n");
		close(bin_fd);
		goto out_unmap;
	}
	bin_buf = malloc(st.st_size);
	read(bin_fd, bin_buf, st.st_size);
	close(bin_fd);

	/* Copy binary to L2 */
	memcpy(l2, bin_buf, st.st_size);
	free(bin_buf);
	printf("Loaded %ld bytes to L2 (phys 0x%08X)\n", st.st_size, L2_PHYS);

	/* Boot cluster */
	req.boot_addr = L2_PHYS;
	req.num_cores = 0;   /* all 12 cores */
	req.result    = 0;

	if (ioctl(fd, CARFIELD_CLUSTER_RUN, &req) < 0) {
		perror("ioctl CARFIELD_CLUSTER_RUN");
		goto out_unmap;
	}

	printf("Cluster finished. Return value: %u\n", req.result);
	if (req.result == 0)
		printf("PASS\n");
	else
		printf("FAIL (non-zero return)\n");

out_unmap:
	munmap(l2, L2_SIZE);
	close(fd);
	return (int)req.result;
}
