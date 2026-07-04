#include "carfield_mock_ot.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/highmem.h>
#include <linux/printk.h>
#include <linux/errno.h>
#include <asm/page.h>

/*
 * Mock OpenTitan consumer -- kthread-based implementation of MOCK_OT_SPEC.md.
 *
 * Clean-room discipline (spec §2.1/§2.2): everything below is derived from
 * the header/map layout in carfield_paging.h and the validation rules in
 * MOCK_OT_SPEC.md §5. It never touches struct carfield_paging_handle or any
 * of carfield_paging.c's in-kernel pointers -- it re-resolves every page
 * from the raw physical addresses, the same way a real, separate piece of
 * silicon would have to.
 */

/* ── Module params (MOCK_OT_SPEC.md §6, plus one test-only addition) ────── */

static int mock_ot;
module_param(mock_ot, int, 0444);
MODULE_PARM_DESC(mock_ot, "Enable the mock OpenTitan kthread consumer (0=off, default)");

static int mock_delay_ms;
module_param(mock_delay_ms, int, 0644);
MODULE_PARM_DESC(mock_delay_ms, "Mock OT: sleep N ms before replying");

static int mock_no_reply;
module_param(mock_no_reply, int, 0644);
MODULE_PARM_DESC(mock_no_reply, "Mock OT: swallow the request silently, never reply");

static int mock_force_err;
module_param(mock_force_err, int, 0644);
MODULE_PARM_DESC(mock_force_err, "Mock OT: always reply with this status code, skip processing entirely");

static int mock_bad_xform;
module_param(mock_bad_xform, int, 0644);
MODULE_PARM_DESC(mock_bad_xform, "Mock OT: XOR with 0xFF instead of 0x5A (proves the test suite isn't vacuous)");

/*
 * Not in MOCK_OT_SPEC.md §6 itself, but directly enables §7's "one §5
 * rejection case" requirement: every §5 rule is something the real
 * producer (carfield_paging_build) already refuses to construct in the
 * first place (bad magic/size/nop/geometry/map never happen if the header
 * came from our own paging chain), so there is no way to reach any of the
 * 8 rejections through the normal ioctl path. This corrupts the magic word
 * of an otherwise-legitimate header right before validating it, so the
 * mock's own §5 check has something real to reject.
 */
static int mock_corrupt_magic;
module_param(mock_corrupt_magic, int, 0644);
MODULE_PARM_DESC(mock_corrupt_magic, "Test-only: corrupt the header magic before validating, to exercise ERR_MAGIC");

#define CARFIELD_MOCK_OT_XOR_KEY	0x5A
#define CARFIELD_MOCK_OT_XOR_KEY_BAD	0xFF

/* ── Channel state (single in-flight request) ────────────────────────────
 *
 * No locking: this mirrors the same "single user/single piece of hardware"
 * assumption already accepted for CARFIELD_CLUSTER_RUN's cdev_data.wq_flag
 * in carfield.c -- concurrent callers were a known, consciously-deferred
 * limitation there, not something this mock introduces new.
 */
struct carfield_mock_ot_channel {
	struct task_struct *thread;

	wait_queue_head_t doorbell_wq;
	bool doorbell_pending;
	u32 letter0_req;
	u32 letter1_req;

	wait_queue_head_t completion_wq;
	bool completion_pending;
	u32 letter0_reply;
	u32 letter1_reply;
};

static struct carfield_mock_ot_channel mock_chan;

/* ── Backend seam (MOCK_OT_SPEC.md §2.4) ─────────────────────────────────── */

void carfield_mock_ot_send(u32 header_phys, u32 cmd)
{
	/* Reset the completion flag before ringing the doorbell -- same
	 * "clear before releasing" discipline as CARFIELD_CLUSTER_RUN's
	 * wq_flag in carfield.c, so a reply that arrives between here and
	 * wait_completion() below isn't missed. */
	mock_chan.completion_pending = false;
	mock_chan.letter0_req = header_phys;
	mock_chan.letter1_req = cmd;
	mock_chan.doorbell_pending = true;
	wake_up_interruptible(&mock_chan.doorbell_wq);
}

int carfield_mock_ot_wait_completion(long timeout_ms)
{
	long left = wait_event_interruptible_timeout(mock_chan.completion_wq,
			mock_chan.completion_pending,
			msecs_to_jiffies(timeout_ms));

	if (left < 0)
		return left;		/* interrupted by a signal */
	if (left == 0)
		return -ETIMEDOUT;	/* mock_no_reply, or a wedged mock */
	return 0;
}

void carfield_mock_ot_read_reply(u32 *letter0, u32 *letter1)
{
	*letter0 = mock_chan.letter0_reply;
	*letter1 = mock_chan.letter1_reply;
	mock_chan.completion_pending = false;
}

int carfield_mock_ot_status_to_errno(u32 status)
{
	switch (status) {
	case CARFIELD_MOCK_OT_OK:		return 0;
	case CARFIELD_MOCK_OT_ERR_MAGIC:	return -EILSEQ;
	case CARFIELD_MOCK_OT_ERR_SIZE:	return -EINVAL;
	case CARFIELD_MOCK_OT_ERR_NOP:		return -E2BIG;
	case CARFIELD_MOCK_OT_ERR_GEOMETRY:	return -EBADMSG;
	case CARFIELD_MOCK_OT_ERR_MAP:		return -EFAULT;
	case CARFIELD_MOCK_OT_ERR_MAP_ENTRY:	return -ENXIO;
	default:				return -EIO; /* mock_force_err or unknown */
	}
}

bool carfield_mock_ot_enabled(void)
{
	return mock_chan.thread != NULL;
}

/* ── Validation (MOCK_OT_SPEC.md §5) ─────────────────────────────────────── */

static u32 carfield_mock_ot_validate(u32 magic, u32 dsz, u32 nop, u32 fpo,
				       u32 fps, u32 lps, u32 map_phys)
{
	u64 expected_nop, expected_fps, expected_lps;

	if (magic != CARFIELD_PAGING_MAGIC)
		return CARFIELD_MOCK_OT_ERR_MAGIC;		/* rule 1 */

	if (dsz == 0)
		return CARFIELD_MOCK_OT_ERR_SIZE;		/* rule 2 */

	if (nop == 0 || nop > 1024)
		return CARFIELD_MOCK_OT_ERR_NOP;		/* rule 3 */

	if (fpo >= PAGE_SIZE)
		return CARFIELD_MOCK_OT_ERR_GEOMETRY;		/* rule 8 */

	expected_nop = ((u64)fpo + dsz + PAGE_SIZE - 1) / PAGE_SIZE;
	if (expected_nop != nop)
		return CARFIELD_MOCK_OT_ERR_GEOMETRY;		/* rule 4 */

	if (nop == 1) {
		expected_fps = dsz;
		expected_lps = dsz;
	} else {
		expected_fps = PAGE_SIZE - fpo;
		expected_lps = (u64)dsz - expected_fps - (u64)(nop - 2) * PAGE_SIZE;
	}
	if (fps != expected_fps || lps != expected_lps)
		return CARFIELD_MOCK_OT_ERR_GEOMETRY;		/* rule 5 */

	if (!map_phys || (map_phys & (PAGE_SIZE - 1)))
		return CARFIELD_MOCK_OT_ERR_MAP;		/* rule 6 */

	return CARFIELD_MOCK_OT_OK;
}

/* ── Independent header/map/data walk (MOCK_OT_SPEC.md §2.2, §3, §4) ────── */

static u32 carfield_mock_ot_process(u32 header_phys)
{
	struct page *hpage, *mpage;
	struct carfield_mbox_header *header;
	u32 *map;
	u32 magic, dsz, nop, fpo, fps, lps, map_phys;
	u32 version, reserved0, reserved1;
	u32 status;
	u32 i;
	u8 xor_key = mock_bad_xform ? CARFIELD_MOCK_OT_XOR_KEY_BAD
				    : CARFIELD_MOCK_OT_XOR_KEY;

	/* Independent walk starts here: header_phys is a raw phys addr off
	 * the wire, not h->header from carfield_paging_build(). */
	hpage = pfn_to_page(PHYS_PFN(header_phys));
	header = kmap_local_page(hpage);
	magic	 = header->magic;
	dsz	 = header->dsz;
	nop	 = header->nop;
	fpo	 = header->fpo;
	fps	 = header->fps;
	lps	 = header->lps;
	map_phys = header->map;

	/*
	 * version/reserved live past the ratified §3 layout (see
	 * carfield_paging.h). MOCK_OT_SPEC.md doesn't define a check for
	 * them yet, so they're read (proving the mock's own struct
	 * definition agrees byte-for-byte with the producer's) and
	 * deliberately not validated -- a version check belongs here once
	 * there's a second version to reject.
	 */
	version   = header->version;
	reserved0 = header->reserved[0];
	reserved1 = header->reserved[1];
	(void)version;
	(void)reserved0;
	(void)reserved1;

	if (mock_corrupt_magic)
		magic ^= 0xFFFFFFFFu;

	kunmap_local(header);

	status = carfield_mock_ot_validate(magic, dsz, nop, fpo, fps, lps, map_phys);
	if (status != CARFIELD_MOCK_OT_OK)
		return status;

	mpage = pfn_to_page(PHYS_PFN(map_phys));
	map = kmap_local_page(mpage);

	/* Rule 7: every map entry must be a non-zero, page-aligned phys addr.
	 * Checked as its own pass, before any page is written, so a bad
	 * entry never causes a partial transform. */
	for (i = 0; i < nop; i++) {
		if (!map[i] || (map[i] & (PAGE_SIZE - 1))) {
			kunmap_local(map);
			return CARFIELD_MOCK_OT_ERR_MAP_ENTRY;
		}
	}

	for (i = 0; i < nop; i++) {
		struct page *dpage;
		u8 *page_va;
		u32 off, len, j;

		if (i == 0) {
			off = fpo;
			len = fps;
		} else if (i == nop - 1) {
			off = 0;
			len = lps;
		} else {
			off = 0;
			len = PAGE_SIZE;
		}

		dpage = pfn_to_page(PHYS_PFN(map[i]));
		page_va = kmap_local_page(dpage);
		for (j = 0; j < len; j++)
			page_va[off + j] ^= xor_key;
		kunmap_local(page_va);
	}

	kunmap_local(map);
	return CARFIELD_MOCK_OT_OK;
}

/* ── Service loop (MOCK_OT_SPEC.md §2.3) ─────────────────────────────────── */

static int carfield_mock_ot_thread_fn(void *arg)
{
	while (!kthread_should_stop()) {
		u32 header_phys, cmd, status;

		wait_event_interruptible(mock_chan.doorbell_wq,
			mock_chan.doorbell_pending || kthread_should_stop());

		if (kthread_should_stop())
			break;

		header_phys = mock_chan.letter0_req;
		cmd = mock_chan.letter1_req;
		(void)cmd; /* part of the doorbell letters; nothing to dispatch
			    * on yet, see the comment below */
		mock_chan.doorbell_pending = false;	/* ack/reset doorbell */

		if (mock_delay_ms > 0)
			msleep(mock_delay_ms);

		if (mock_no_reply) {
			pr_info("carfield_mock_ot: mock_no_reply=1, swallowing request (header_phys=0x%x)\n",
				header_phys);
			continue;	/* no completion signaled -> host times out */
		}

		/*
		 * CMD_XFORM is the only command MOCK_OT_SPEC.md defines, so
		 * there is nothing to dispatch on yet -- cmd is read (it's
		 * part of the doorbell letters) but not branched on. When a
		 * second command exists, switch on it here instead of
		 * guessing at an error code for "unknown" ahead of need.
		 */
		if (mock_force_err)
			status = mock_force_err;
		else
			status = carfield_mock_ot_process(header_phys);

		mock_chan.letter0_reply = header_phys;
		mock_chan.letter1_reply = status;
		mock_chan.completion_pending = true;
		wake_up_interruptible(&mock_chan.completion_wq);
	}

	return 0;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

int carfield_mock_ot_start(void)
{
	if (!mock_ot)
		return 0; /* spec: no behavior change when mock_ot=0 */

	init_waitqueue_head(&mock_chan.doorbell_wq);
	init_waitqueue_head(&mock_chan.completion_wq);

	mock_chan.thread = kthread_run(carfield_mock_ot_thread_fn, NULL,
					"carfield_mock_ot");
	if (IS_ERR(mock_chan.thread)) {
		int ret = PTR_ERR(mock_chan.thread);

		mock_chan.thread = NULL;
		pr_err("carfield_mock_ot: kthread_run failed: %d\n", ret);
		return ret;
	}

	pr_info("carfield_mock_ot: mock OpenTitan kthread started\n");
	return 0;
}

void carfield_mock_ot_stop(void)
{
	if (mock_chan.thread) {
		kthread_stop(mock_chan.thread);
		mock_chan.thread = NULL;
	}
}
