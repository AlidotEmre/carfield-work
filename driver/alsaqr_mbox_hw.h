#ifndef ALSAQR_MBOX_HW_H
#define ALSAQR_MBOX_HW_H

/*
 * Real hardware backend for the mailbox channel seam defined by
 * alsaqr_mock_ot.h (MOCK_OT_SPEC.md §2.4): send() + wait_completion() +
 * read_reply(). This file is the second implementation of that seam --
 * mailbox registers + IRQ instead of a kthread + shared struct.
 *
 * Register map: AlSaqr's real, generated device-tree node for the OpenTitan
 * mailbox (alsaqr-fpga-ecs/dts/generate_dts.py):
 *
 *   ot_mbox@10404000 {
 *     compatible = "opentitan_mbox-0.0";
 *     reg = <0x0 0x10404000 0x0 0x28>;
 *     interrupt-parent = <&PLIC0>;
 *     interrupts = <10>;
 *   };
 *
 * This supersedes the earlier Carfield-specific map (base 0x40000000,
 * id*stride multi-mailbox scheme, hardcoded IRQ 58) -- that map was never
 * confirmed against real hardware (FPGA session still pending). This one is:
 * alsaqr-fpga-ecs/develop/titanssl/titanssl_driver/driver.c already runs
 * against these exact addresses in production (see docs/TITANSSL_ANALYSIS.md).
 *
 * AlSaqr has a single host<->OT mailbox, no separate PULP-side channel (same
 * conclusion already reached independently for Carfield -- "PULP'a komuta
 * mailbox'ı yok", see memory/project_alsaqr.md) -- so unlike the old map,
 * there is no id/stride indirection and no second inbound channel to
 * provision.
 *
 * Deliberately no kernel headers above this line -- the register-offset
 * macros below are pure integer constants, so this half can be included
 * standalone by tests/mbox_reg_test.c with plain gcc, the same split
 * alsaqr_paging.h uses for alsaqr_paging_compute_info().
 */

#define ALSAQR_MBOX_BASE_ADDR	0x10404000UL
#define ALSAQR_MBOX_UNIT_SIZE		0x28	/* full reg span per the DT node above */

/*
 * MBOX word area: where the outbound message (header_phys, cmd) is written
 * before ringing the doorbell. 0x14 bytes (5 words) available per the real
 * device (matches titanssl's own MBOX_SIZE); this driver's wire format only
 * uses the first two words (header_phys, cmd) -- see the note in
 * alsaqr_mbox_hw.c's send(). Words 2-4 are left unused/zero, not a
 * titanssl-style 5-field ABI (magic/cmd+bitfield/session/pid) -- that ABI
 * belongs to titanssl's own firmware, not ours (we have no OT firmware yet,
 * see docs/QUESTIONS_FOR_TEAM.md).
 */
#define ALSAQR_MBOX_REG_WORD0		0x00	/* header_phys */
#define ALSAQR_MBOX_REG_WORD1		0x04	/* cmd */
#define ALSAQR_MBOX_WORD_AREA_SIZE	0x14	/* 5 x u32, matches titanssl MBOX_SIZE */

/* 0x14-0x20 is reserved/unused per the DT node's 0x28-byte span -- not a
 * gap in this driver's own layout, just unused hardware address space
 * between the word area and the trigger registers below. */

#define ALSAQR_MBOX_REG_DOORBELL	0x20	/* write-1 to ring (host -> OT) */
#define ALSAQR_MBOX_REG_COMPLETION	0x24	/* write-0 to ack after wake (OT -> host) */

/*
 * DT compatible string this driver's platform_driver matches against, to
 * obtain the real IRQ number via platform_get_irq() instead of a hardcoded
 * placeholder -- same pattern as titanssl_driver/driver.c's ot_mbox_probe()
 * (see docs/TITANSSL_ANALYSIS.md §5 "RESERVE": IRQ acquisition via
 * devicetree/platform_device). Unlike Carfield's old CARFIELD_MBOX_IRQ=58
 * (never confirmed against real hardware), the DT node above is generated,
 * authoritative source -- so this is read dynamically rather than
 * hardcoded, in case the generated DT ever changes the source ID.
 */
#define ALSAQR_MBOX_DT_COMPATIBLE	"opentitan_mbox-0.0"

#ifdef __KERNEL__

#include <linux/types.h>

/*
 * Starts the hardware backend iff the real_mbox module param is set; a
 * no-op (returns 0) when real_mbox=0, mirroring alsaqr_mock_ot_start()'s
 * "no behavior change when off" contract. Call once from alsaqr_init(),
 * AFTER the mutual-exclusion check against mock_ot (see alsaqr.c) --
 * this function does not itself refuse to run alongside the mock, that
 * guard lives in alsaqr_init() where both module params are visible.
 */
int alsaqr_mbox_hw_start(void);

/* Stops the backend (frees the IRQ if requested, iounmaps if mapped,
 * unregisters the platform_driver). Call from alsaqr_exit(). Safe to call
 * even if start() was a no-op. */
void alsaqr_mbox_hw_stop(void);

/* True iff real_mbox=1 was requested (independent of whether ioremap/IRQ
 * actually succeeded) -- used only for the mock_ot/real_mbox mutual
 * exclusion check in alsaqr_init(), before either backend has started. */
bool alsaqr_mbox_hw_requested(void);

/* True iff the hardware backend is both requested AND actually ready to
 * send (ioremap succeeded AND the DT-matched IRQ was obtained/requested).
 * The ioctl uses this to decide which backend to dispatch to, and to fail
 * with -ENODEV instead of calling send() against an unmapped region. */
bool alsaqr_mbox_hw_enabled(void);

/*
 * Backend seam (MOCK_OT_SPEC.md §2.4), hardware version of
 * alsaqr_mock_ot_send()/_wait_completion()/_read_reply(). Same
 * signatures, so the ioctl caller in alsaqr.c only needs to pick which
 * set of three functions to call, not restructure its flow.
 *
 * send(): host -> OT doorbell. wait_completion()/read_reply() observe the
 * single OT -> host completion signal.
 */
void alsaqr_mbox_hw_send(u32 header_phys, u32 cmd);
int alsaqr_mbox_hw_wait_completion(long timeout_ms);
void alsaqr_mbox_hw_read_reply(u32 *letter0, u32 *letter1);

#endif /* __KERNEL__ */

#endif /* ALSAQR_MBOX_HW_H */
