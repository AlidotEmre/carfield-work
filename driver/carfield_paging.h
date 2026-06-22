#ifndef CARFIELD_PAGING_H
#define CARFIELD_PAGING_H

#include <linux/types.h>
#include "carfield.h"		/* CARFIELD_MAGIC, shared by every /dev/carfield ioctl */

#ifdef __KERNEL__
#include <linux/mm_types.h>
#endif

/*
 * Carfield paging chain.
 *
 * The mailbox only carries a single 32-bit physical address: the address
 * of a "header page". Everything the receiver (PULP/OpenTitan) needs to
 * know about the actual data buffer lives inside that page.
 *
 * Reference: alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/{driver.c,
 * titanssl.h} implements the same chain for three buffer categories
 * (input/output/meta). Carfield's current use case is a single encrypted
 * blob, so this is the single-category version of that struct.
 *
 * Field meaning (mirrors titanssl_mbox_header_t):
 *   dsz - data size in bytes
 *   nop - number of pages the buffer spans
 *   fpo - first page offset (data_addr % PAGE_SIZE)
 *   fps - bytes used in the first page
 *   lps - bytes used in the last page
 *   map - physical address of the "map page": an array of nop u32 entries,
 *         each holding the physical address of one of the buffer's pages
 *         (in order). The receiver walks this array to find every page.
 */
struct carfield_mbox_header {
	__u32 magic;
	__u32 dsz;
	__u32 nop;
	__u32 fpo;
	__u32 fps;
	__u32 lps;
	__u32 map;
} __attribute__((__packed__));

#define CARFIELD_PAGING_MAGIC 0xCA4F1E1D

/*
 * Page-layout math for a [data_addr, data_addr + data_size) user range.
 * Pure arithmetic, no kernel/page-table access -- computed once and reused
 * by both the pinning step and the header-fill step.
 */
struct carfield_page_info {
	unsigned long data_addr;
	unsigned long data_size;
	unsigned long page_size;
	unsigned long nop;
	unsigned long fpo;
	unsigned long fps;
	unsigned long lps;
};

void carfield_paging_compute_info(unsigned long data_addr,
				   unsigned long data_size,
				   unsigned long page_size,
				   struct carfield_page_info *info);

#ifdef __KERNEL__

/*
 * Tracks every resource a single carfield_paging_build() call allocates,
 * so carfield_paging_release() can unwind all of it in one place.
 */
struct carfield_paging_handle {
	struct carfield_page_info info;

	struct page *header_page;
	struct page *map_page;
	struct page **data_pages;	/* nop entries, pinned */

	struct carfield_mbox_header *header;	/* kmap of header_page */
	__u32 *map;				/* kmap of map_page */

	phys_addr_t header_phys;	/* what to write into the mailbox */
};

/*
 * Pin the user range [user_addr, user_addr + user_size), build the header
 * + map pages, and fill them in. On success *out is ready to hand off:
 * out->header_phys is the 32-bit-safe physical address to send via the
 * mailbox. Call carfield_paging_release() exactly once when done, whether
 * this returns success or failure partway through is handled internally.
 *
 * write: pass true if the receiver will write back into this buffer
 * (FOLL_WRITE for get_user_pages), false for read-only input buffers.
 */
int carfield_paging_build(unsigned long user_addr, unsigned long user_size,
			   bool write, struct carfield_paging_handle *out);

void carfield_paging_release(struct carfield_paging_handle *h);

#endif /* __KERNEL__ */

/* ── Userspace test ioctl (no FPGA/mailbox needed) ──────────────────────── */

/*
 * Pins the given user buffer, builds the header+map pages exactly as a
 * real transfer would, then immediately reads back the computed fields
 * and the first/last entries of the map -- without ever touching the
 * mailbox. Validates the pin/build/release chain on real hardware-backed
 * physical addresses, no FPGA required.
 */
struct carfield_paging_test_req {
	__u64 user_addr;
	__u64 user_size;

	/* outputs */
	__u32 dsz;
	__u32 nop;
	__u32 fpo;
	__u32 fps;
	__u32 lps;
	__u64 header_phys;
	__u64 first_page_phys;	/* map[0] */
	__u64 last_page_phys;	/* map[nop-1] */
};

#define CARFIELD_PAGING_TEST \
	_IOWR(CARFIELD_MAGIC, 2, struct carfield_paging_test_req)

#endif /* CARFIELD_PAGING_H */
