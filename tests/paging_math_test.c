/*
 * Userspace test for carfield_paging_compute_info() (driver/carfield_paging_math.c).
 *
 * This validates only the page-layout arithmetic (nop/fpo/fps/lps) -- it
 * does not pin real pages or touch physical addresses, since that needs
 * a real kernel build (no WSL2 kernel-headers package available right
 * now; see driver/carfield_paging.c for the real pin/build/release code).
 * Plain gcc, no FPGA, no kernel module required.
 */

#include <stdio.h>
#include "../driver/carfield_paging.h"

static int failures;

static void check_case(const char *name, unsigned long addr,
			unsigned long size, unsigned long page_size,
			unsigned long exp_nop, unsigned long exp_fpo,
			unsigned long exp_fps, unsigned long exp_lps)
{
	struct carfield_page_info info;

	carfield_paging_compute_info(addr, size, page_size, &info);

	printf("[%-28s] addr=0x%05lx size=%6lu -> nop=%lu fpo=%lu fps=%lu lps=%lu\n",
	       name, addr, size, info.nop, info.fpo, info.fps, info.lps);

	if (info.nop != exp_nop || info.fpo != exp_fpo ||
	    info.fps != exp_fps || info.lps != exp_lps) {
		printf("    FAIL: expected nop=%lu fpo=%lu fps=%lu lps=%lu\n",
		       exp_nop, exp_fpo, exp_fps, exp_lps);
		failures++;
	}
}

int main(void)
{
	unsigned long page_size = 4096;

	printf("== Carfield paging math test (no kernel/FPGA needed) ==\n\n");

	/* Single page, page-aligned, fits entirely in one page */
	check_case("aligned, fits in 1 page", 0x1000, 100, page_size,
		   /*nop*/ 1, /*fpo*/ 0, /*fps*/ 100, /*lps*/ 100);

	/* Single page, unaligned start, still fits in that one page */
	check_case("unaligned, fits in 1 page", 0x1010, 100, page_size,
		   1, 0x10, 100, 100);

	/* Exactly fills one page from offset 0 */
	check_case("exactly fills 1 page", 0x2000, page_size, page_size,
		   1, 0, page_size, page_size);

	/* Spans exactly 2 pages, aligned start */
	check_case("aligned, spans 2 pages", 0x3000, page_size + 1, page_size,
		   2, 0, page_size, 1);

	/* Unaligned start that spills into a second page */
	check_case("unaligned, spills into 2nd page", 0x4FF0, 32, page_size,
		   2, 0xFF0, 16, 16);

	/* Spans many pages, unaligned start and end */
	check_case("unaligned, spans many pages", 0x5123, 10000, page_size,
		   3, 0x123, page_size - 0x123, 2099);

	printf("\n");
	if (failures) {
		printf("== FAIL: %d case(s) failed ==\n", failures);
		return 1;
	}
	printf("== PASS: all cases matched ==\n");
	return 0;
}
