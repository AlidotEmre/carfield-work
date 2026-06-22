#include "carfield_paging.h"

#include <linux/mm.h>		/* pin_user_pages_fast / unpin_user_pages */
#include <linux/highmem.h>	/* kmap / kunmap */
#include <linux/gfp.h>		/* alloc_page */
#include <asm/page.h>		/* PAGE_SIZE */
#include <linux/slab.h>		/* kmalloc / kfree */
#include <linux/string.h>	/* memset */

/* carfield_paging_compute_info() lives in carfield_paging_math.c */

#ifdef __KERNEL__

/*
 * Pin nop user pages starting at info->data_addr.
 *
 * Uses pin_user_pages_fast(), the modern replacement for
 * get_user_pages_fast()+FOLL_GET used in the titanssl reference --
 * pin_user_pages is the API the mm subsystem expects callers that hold
 * pages for DMA/hardware access (rather than just reading them) to use,
 * and it pairs with unpin_user_pages() below.
 */
static struct page **carfield_paging_get_pages(struct carfield_page_info *info,
						 bool write)
{
	struct page **pages;
	unsigned int flags = write ? FOLL_WRITE : 0;
	long got;

	pages = kmalloc_array(info->nop, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;

	got = pin_user_pages_fast(info->data_addr, info->nop, flags, pages);
	if (got != info->nop) {
		if (got > 0)
			unpin_user_pages(pages, got);
		kfree(pages);
		return NULL;
	}

	return pages;
}

static void carfield_paging_put_pages(struct page **pages, unsigned long nop)
{
	unpin_user_pages(pages, nop);
	kfree(pages);
}

int carfield_paging_build(unsigned long user_addr, unsigned long user_size,
			   bool write, struct carfield_paging_handle *out)
{
	unsigned long i;

	memset(out, 0, sizeof(*out));
	carfield_paging_compute_info(user_addr, user_size, PAGE_SIZE,
				      &out->info);

	/*
	 * GFP_DMA32: the receiver (PULP/OpenTitan) only has a 32-bit
	 * address bus, and the mailbox header_phys field is a u32. The
	 * titanssl reference allocates these with plain GFP_KERNEL and
	 * truncates the resulting phys_addr_t with "& 0xFFFFFFFF" when
	 * writing the mailbox -- on a 64-bit host with memory above 4GB
	 * that silently corrupts the address instead of failing. GFP_DMA32
	 * makes the allocator guarantee a <4GB physical page up front.
	 */
	out->header_page = alloc_page(GFP_KERNEL | GFP_DMA32);
	if (!out->header_page)
		return -ENOMEM;

	out->map_page = alloc_page(GFP_KERNEL | GFP_DMA32);
	if (!out->map_page)
		goto err_free_header;

	out->data_pages = carfield_paging_get_pages(&out->info, write);
	if (!out->data_pages)
		goto err_free_map;

	out->header = kmap(out->header_page);
	out->map    = kmap(out->map_page);

	for (i = 0; i < out->info.nop; i++)
		out->map[i] = (u32)page_to_phys(out->data_pages[i]);

	out->header_phys = page_to_phys(out->header_page);

	out->header->magic = CARFIELD_PAGING_MAGIC;
	out->header->dsz   = out->info.data_size;
	out->header->nop   = out->info.nop;
	out->header->fpo   = out->info.fpo;
	out->header->fps   = out->info.fps;
	out->header->lps   = out->info.lps;
	out->header->map   = (u32)page_to_phys(out->map_page);

	return 0;

err_free_map:
	__free_page(out->map_page);
	out->map_page = NULL;
err_free_header:
	__free_page(out->header_page);
	out->header_page = NULL;
	return -ENOMEM;
}

void carfield_paging_release(struct carfield_paging_handle *h)
{
	if (h->data_pages)
		carfield_paging_put_pages(h->data_pages, h->info.nop);

	if (h->header)
		kunmap(h->header_page);
	if (h->map)
		kunmap(h->map_page);

	if (h->header_page)
		__free_page(h->header_page);
	if (h->map_page)
		__free_page(h->map_page);
}

#endif /* __KERNEL__ */
