#ifndef ALSAQR_MOCK_OT_H
#define ALSAQR_MOCK_OT_H

#include <linux/types.h>
#include "alsaqr.h"
#include "alsaqr_paging.h"	/* struct alsaqr_mbox_header, ALSAQR_PAGING_MAGIC --
				 * the mock validates against the SAME magic/layout the
				 * real header uses, not a second hand-maintained copy
				 * (see TITANSSL_ANALYSIS.md §1.4 for why that class of
				 * drift is worth avoiding) */

/*
 * Mock OpenTitan consumer -- see MOCK_OT_SPEC.md for the full contract.
 *
 * Doorbell: letter0 = header_phys, letter1 = command.
 * Reply:    letter0 echoed, letter1 = status.
 */

/* Doorbell command word (MOCK_OT_SPEC.md §3) */
#define ALSAQR_OT_CMD_XFORM	0x0001

/* Reply status codes (MOCK_OT_SPEC.md §5) -- distinct values so the host can
 * map each to a distinct errno (see alsaqr_mock_ot_status_to_errno()). */
#define ALSAQR_MOCK_OT_OK		0
#define ALSAQR_MOCK_OT_ERR_MAGIC	1
#define ALSAQR_MOCK_OT_ERR_SIZE	2
#define ALSAQR_MOCK_OT_ERR_NOP	3
#define ALSAQR_MOCK_OT_ERR_GEOMETRY	4
#define ALSAQR_MOCK_OT_ERR_MAP	5
#define ALSAQR_MOCK_OT_ERR_MAP_ENTRY	6

/* Sentinel written into alsaqr_ot_xform_req.ot_status when the ioctl
 * returns without ever getting a reply (mock_no_reply, or a signal). */
#define ALSAQR_OT_STATUS_NONE	0xFFFFFFFFu

#ifdef __KERNEL__

/*
 * Starts the mock kthread iff the mock_ot module param is set; a no-op
 * (returns 0) when mock_ot=0, per MOCK_OT_SPEC.md's "no behavior change
 * when mock_ot=0" requirement. Call once from alsaqr_init().
 */
int alsaqr_mock_ot_start(void);

/* Stops and joins the kthread if it was started. Call from alsaqr_exit(). */
void alsaqr_mock_ot_stop(void);

/* True once the kthread is actually running (i.e. mock_ot=1 and start
 * succeeded). The ioctl uses this to fail with -ENODEV instead of hanging
 * forever waiting for a consumer that doesn't exist. */
bool alsaqr_mock_ot_enabled(void);

/* True iff mock_ot=1 was requested, independent of whether the kthread
 * actually started -- used only for the mock_ot/real_mbox mutual exclusion
 * check in alsaqr_init(), which runs before either backend has started. */
bool alsaqr_mock_ot_requested(void);

/*
 * Backend seam (MOCK_OT_SPEC.md §2.4): send() + wait_completion() +
 * read_reply(). This is the mock backend (shared state + waitqueues); a
 * later hardware backend (mailbox registers + IRQ) would implement the same
 * three calls without the ioctl-side caller changing at all.
 */
void alsaqr_mock_ot_send(u32 header_phys, u32 cmd);
int alsaqr_mock_ot_wait_completion(long timeout_ms);
void alsaqr_mock_ot_read_reply(u32 *letter0, u32 *letter1);

/* Maps a §5 status code (or an out-of-range one from mock_force_err) to a
 * distinct errno for the ioctl to return. */
int alsaqr_mock_ot_status_to_errno(u32 status);

#endif /* __KERNEL__ */

/* ── Userspace ioctl: run the mock consumer over a user buffer ──────────── */

/*
 * Builds the paging chain over [user_addr, user_addr + user_size) exactly
 * like ALSAQR_PAGING_TEST, then actually sends it to the mock OT consumer
 * and waits for the reply. ot_status is the raw §5 status code (0 = OK);
 * the ioctl's own return value is the errno-mapped version of the same
 * thing, or -ENODEV if mock_ot=0, or -ETIMEDOUT if the mock never replied.
 */
struct alsaqr_ot_xform_req {
	__u64 user_addr;
	__u64 user_size;

	__u32 ot_status;
};

#define ALSAQR_OT_XFORM \
	_IOWR(ALSAQR_MAGIC, 3, struct alsaqr_ot_xform_req)

#endif /* ALSAQR_MOCK_OT_H */
