/*
 * Userspace test for CARFIELD_PAGING_TEST (driver/carfield_paging.c).
 *
 * Unlike paging_math_test.c (pure arithmetic, no kernel needed), this
 * exercises the real pin_user_pages_fast() -> header/map page -> release
 * chain against /dev/carfield on a real kernel. Requires the carfield_mod
 * module to be loaded (no FPGA/mailbox involved -- the ioctl stops right
 * after building the header, before anything would be sent to hardware).
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "../driver/carfield_paging.h"

#define TEST_PAGE_SIZE 4096

int main(void)
{
	int fd;
	void *buf;
	unsigned long fpo, expect_nop;
	struct carfield_paging_test_req req;
	int rc = 0;

	/* 4 pages of anonymous memory, touched so every page is resident
	 * before we ask the kernel to pin it. */
	buf = mmap(NULL, 4 * TEST_PAGE_SIZE, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	memset(buf, 0xAB, 4 * TEST_PAGE_SIZE);

	/* Unaligned start (0x123 into page 0), size spans into page 2 --
	 * same shape as the "unaligned, spans many pages" math test case,
	 * so nop is known ahead of time. */
	fpo = 0x123;
	expect_nop = 3;

	fd = open("/dev/carfield", O_RDWR);
	if (fd < 0) {
		perror("open /dev/carfield");
		munmap(buf, 4 * TEST_PAGE_SIZE);
		return 1;
	}

	memset(&req, 0, sizeof(req));
	req.user_addr = (__u64)(unsigned long)buf + fpo;
	req.user_size = 2 * TEST_PAGE_SIZE + 100;

	if (ioctl(fd, CARFIELD_PAGING_TEST, &req) < 0) {
		perror("ioctl CARFIELD_PAGING_TEST");
		close(fd);
		munmap(buf, 4 * TEST_PAGE_SIZE);
		return 1;
	}

	printf("dsz=%u nop=%u fpo=%u fps=%u lps=%u\n",
	       req.dsz, req.nop, req.fpo, req.fps, req.lps);
	printf("header_phys=0x%llx first_page_phys=0x%llx last_page_phys=0x%llx\n",
	       (unsigned long long)req.header_phys,
	       (unsigned long long)req.first_page_phys,
	       (unsigned long long)req.last_page_phys);

	if (req.nop != expect_nop || req.fpo != fpo ||
	    req.dsz != req.user_size ||
	    !req.header_phys || !req.first_page_phys || !req.last_page_phys) {
		printf("FAIL: unexpected values (expected nop=%lu fpo=%lu)\n",
		       expect_nop, fpo);
		rc = 1;
	} else {
		printf("PASS: pin/build/release chain exercised on real kernel\n");
	}

	close(fd);
	munmap(buf, 4 * TEST_PAGE_SIZE);
	return rc;
}
