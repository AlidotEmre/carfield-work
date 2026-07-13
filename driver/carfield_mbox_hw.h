#ifndef CARFIELD_MBOX_HW_H
#define CARFIELD_MBOX_HW_H

/*
 * Real hardware backend for the mailbox channel seam defined by
 * carfield_mock_ot.h (MOCK_OT_SPEC.md §2.4): send() + wait_completion() +
 * read_reply(). This file is the second implementation of that seam --
 * mailbox registers + IRQ instead of a kthread + shared struct.
 *
 * Register map confirmed against Daniele's answers (docs/QUESTIONS_FOR_TEAM.md)
 * and the real mbox.h he shared (2026-07-06), NOT guessed:
 *   base + id*CARFIELD_MBOX_STRIDE + <offset>
 * Ported from mailbox-simulation-withoutFPGA/carfield_mbox.c (carfield_mbox_sim
 * repo), which already carries this same register map in kernel-API-shaped
 * code (writel/readl/__iomem) validated end-to-end in userspace simulation.
 *
 * Deliberately no kernel headers above this line -- the register-offset
 * macros and carfield_mbox_reg_addr() below are pure integer arithmetic, so
 * this half can be included standalone by tests/mbox_reg_test.c with plain
 * gcc, the same split carfield_paging.h uses for carfield_paging_compute_info().
 */

#define CARFIELD_MBOX_BASE_ADDR	0x40000000UL
#define CARFIELD_MBOX_STRIDE		0x100

/* Sender (SND) side -- the only side actually wired to an interrupt line
 * per Daniele's 2026-07-09 confirmation (all three directions use
 * INT_SND_SET as the doorbell). RCV_* offsets exist in the real mbox.h but
 * are NOT used here -- see the INT_RCV_SET note below. */
#define CARFIELD_MBOX_REG_SND_STAT	0x00
#define CARFIELD_MBOX_REG_SND_SET	0x04
#define CARFIELD_MBOX_REG_SND_CLR	0x08
#define CARFIELD_MBOX_REG_SND_EN	0x0C

#define CARFIELD_MBOX_REG_LETTER0	0x80
#define CARFIELD_MBOX_REG_LETTER1	0x84	/* confirmed 0x84, NOT 0x8C */

#define CARFIELD_MBOX_ID_HOST_TO_OT	1
#define CARFIELD_MBOX_ID_PULP_TO_HOST	5
#define CARFIELD_MBOX_ID_OT_TO_HOST	7

/* ioremap window: covers mailbox ids 0..CARFIELD_MBOX_ID_OT_TO_HOST
 * inclusive (all confirmed-real mailbox register space by the id*stride
 * formula above), NOT a confirmed total size of the real mailbox IP block.
 * If the real unit turns out smaller or larger, this is the one place to
 * change -- see the FPGA-session note in carfield_mbox_hw.c's start(). */
#define CARFIELD_MBOX_UNIT_SIZE \
	((CARFIELD_MBOX_ID_OT_TO_HOST + 1) * CARFIELD_MBOX_STRIDE)

/*
 * CONFIRMED (2026-07-13): `HOST_MBOX_IRQ 58` is defined in Daniele's real
 * `car_lib_mbox.h` (OpenTitan ROM-side header), next to
 * `HOST_TO_CLUSTER_MBOX`/`CLUSTER_MBOX_EVT` -- those two were separately
 * refuted by Daniele as dead/misleading, but no equivalent refutation ever
 * came for HOST_MBOX_IRQ, so it's accepted as real. Supersedes the earlier
 * "58 is unconfirmed, only appears as MOCK_OT_SPEC.md §8 filler text"
 * finding -- that finding was accurate against the sources checked at the
 * time, it just turned out there was a better source (this header) that
 * hadn't been checked yet.
 */
#define CARFIELD_MBOX_IRQ		58

/*
 * Genuinely open (docs/QUESTIONS_FOR_TEAM.md item 6): whether the header
 * page should live in L2 instead of DRAM, and at what offset within
 * whichever of the two contradictory L2 layouts turns out real. Header
 * stays in DRAM (GFP_DMA32, carfield_paging_build()) for now -- this define
 * is unused by any code path, purely a marker for where that offset would
 * go once the L2 question resolves. TODO(daniele-fpga-session).
 */
#define CARFIELD_HEADER_L2_OFFSET	0

/* Absolute register offset (relative to CARFIELD_MBOX_BASE_ADDR) for a
 * given mailbox id + register offset. Pure arithmetic -- no __iomem here,
 * see carfield_mbox_hw.c for the version that adds this to a mapped base
 * pointer. */
static inline unsigned long carfield_mbox_reg_addr(unsigned int id,
						     unsigned long off)
{
	return (unsigned long)id * CARFIELD_MBOX_STRIDE + off;
}

#ifdef __KERNEL__

#include <linux/types.h>

/*
 * Starts the hardware backend iff the real_mbox module param is set; a
 * no-op (returns 0) when real_mbox=0, mirroring carfield_mock_ot_start()'s
 * "no behavior change when off" contract. Call once from carfield_init(),
 * AFTER the mutual-exclusion check against mock_ot (see carfield.c) --
 * this function does not itself refuse to run alongside the mock, that
 * guard lives in carfield_init() where both module params are visible.
 */
int carfield_mbox_hw_start(void);

/* Stops the backend (frees the IRQ if requested, iounmaps if mapped).
 * Call from carfield_exit(). Safe to call even if start() was a no-op. */
void carfield_mbox_hw_stop(void);

/* True iff real_mbox=1 was requested (independent of whether ioremap/IRQ
 * actually succeeded) -- used only for the mock_ot/real_mbox mutual
 * exclusion check in carfield_init(), before either backend has started. */
bool carfield_mbox_hw_requested(void);

/* True iff the hardware backend is both requested AND actually ready to
 * send (ioremap of the mailbox unit succeeded). The ioctl uses this to
 * decide which backend to dispatch to, and to fail with -ENODEV instead of
 * calling send() against an unmapped region. */
bool carfield_mbox_hw_enabled(void);

/*
 * Backend seam (MOCK_OT_SPEC.md §2.4), hardware version of
 * carfield_mock_ot_send()/_wait_completion()/_read_reply(). Same
 * signatures, so the ioctl caller in carfield.c only needs to pick which
 * set of three functions to call, not restructure its flow.
 *
 * send(): host -> OT doorbell (mailbox id 1). wait_completion()/read_reply()
 * only observe mailbox id 7 (OT -> host reply) -- mailbox id 5 (PULP ->
 * host) is provisioned (IRQ registered, INT_SND_EN set) so its
 * level-sensitive line gets acked instead of wedging the shared IRQ, but
 * nothing consumes it yet; there is no PULP-side consumer to talk to (Event
 * Unit work, out of scope here per spec §4).
 */
void carfield_mbox_hw_send(u32 header_phys, u32 cmd);
int carfield_mbox_hw_wait_completion(long timeout_ms);
void carfield_mbox_hw_read_reply(u32 *letter0, u32 *letter1);

#endif /* __KERNEL__ */

#endif /* CARFIELD_MBOX_HW_H */
