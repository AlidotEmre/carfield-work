#include "carfield_paging.h"

/*
 * Page-layout math for a [data_addr, data_addr + data_size) range.
 *
 * Ported from titanssl_helper_page_info() (titanssl_driver/driver.c) --
 * same algorithm, kept verbatim because it is correct and already proven
 * against real hardware there.
 *
 * Deliberately kept in its own translation unit with no kernel headers:
 * it's pure integer arithmetic, so it can be linked into the kernel
 * module AND compiled standalone with plain gcc for a userspace test
 * (see tests/paging_math_test.c) -- no need for a real kernel build to
 * verify this logic.
 */
void carfield_paging_compute_info(unsigned long data_addr,
				   unsigned long data_size,
				   unsigned long page_size,
				   struct carfield_page_info *info)
{
	unsigned long fpo, fps, lps, nop;
	unsigned long data_addr_end, first_page_id, last_page_id;

	fpo = data_addr % page_size;

	data_addr_end = data_addr + data_size - 1;
	first_page_id = data_addr / page_size;
	last_page_id  = data_addr_end / page_size;
	nop = last_page_id - first_page_id + 1;

	if (data_size > page_size - fpo) {
		fps = page_size - fpo;
		lps = page_size - (page_size * nop - data_size - fpo);
	} else {
		fps = data_size;
		lps = data_size;
	}

	info->data_addr = data_addr;
	info->data_size = data_size;
	info->page_size = page_size;
	info->nop = nop;
	info->fpo = fpo;
	info->fps = fps;
	info->lps = lps;
}
