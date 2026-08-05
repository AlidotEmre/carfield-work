/*
 * Phase 2 test: load a binary into L2 via mmap, notify OpenTitan to boot
 * the PULP cluster from it, and print OT's reply status.
 *
 * The host no longer boots the cluster directly -- per Daniele's
 * 2026-07-30 code review, OpenTitan is the only thing that can, and how
 * it does so is out of scope/black box here (see
 * memory/project_alsaqr.md). This test only exercises the host<->OT
 * notification (ALSAQR_CLUSTER_RUN), same as ALSAQR_OT_XFORM's
 * seam. Whether OT's reply also means "cluster finished running", or
 * only "OT accepted the boot request", is still an open question
 * (docs/QUESTIONS_FOR_TEAM.md) -- this test cannot currently tell the
 * two apart.
 *
 * Usage:
 *   ./cluster_test <binary.bin>
 *
 * <binary.bin> is the stripped ELF / raw binary of pulp_hello compiled
 * with pulp-runtime. Without FPGA hardware this test will fail at mmap().
 * Requires the module loaded with mock_ot=1 or real_mbox=1.
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
#include "../driver/alsaqr.h"

#define L2_SIZE  0x100000   /* 1 MB */
#define L2_PHYS  0x78000000 /* physical base of L2_INTL_0 */

int main(int argc, char *argv[])
{
	int fd, bin_fd;
	void *l2;
	struct stat st;
	void *bin_buf;
	struct alsaqr_cluster_run req;
	int rc = 1; /* default to failure; only cleared on confirmed PASS */

	if (argc != 2) {
		fprintf(stderr, "usage: %s <binary.bin>\n", argv[0]);
		return 1;
	}

	/* Open Alsaqr device */
	fd = open("/dev/alsaqr", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("open /dev/alsaqr");
		return 1;
	}

	/* Map L2 interleaved bank 0 into user space */
	l2 = mmap(NULL, L2_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		  fd, ALSAQR_MMAP_L2_INTL_0 * getpagesize());
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
	if (!bin_buf) {
		perror("malloc");
		close(bin_fd);
		goto out_unmap;
	}
	if (read(bin_fd, bin_buf, st.st_size) != st.st_size) {
		perror("read binary");
		free(bin_buf);
		close(bin_fd);
		goto out_unmap;
	}
	close(bin_fd);

	/* Copy binary to L2 */
	memcpy(l2, bin_buf, st.st_size);
	free(bin_buf);
	printf("Loaded %ld bytes to L2 (phys 0x%08X)\n", st.st_size, L2_PHYS);

	/* Notify OpenTitan to boot the cluster from L2_PHYS -- the host
	 * cannot boot it directly (see file header comment). */
	req.boot_addr = L2_PHYS;
	req.result    = 0;

	if (ioctl(fd, ALSAQR_CLUSTER_RUN, &req) < 0) {
		perror("ioctl ALSAQR_CLUSTER_RUN");
		goto out_unmap;
	}

	printf("OT replied. Status: %u\n", req.result);
	if (req.result == 0) {
		printf("PASS\n");
		rc = 0;
	} else {
		printf("FAIL (non-zero OT status)\n");
	}

out_unmap:
	munmap(l2, L2_SIZE);
	close(fd);
	return rc;
}
