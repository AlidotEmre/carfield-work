#include "alsaqr_mbox_hw.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

/*
 * Real hardware backend -- see alsaqr_mbox_hw.h for the register map
 * (AlSaqr's single host<->OT OpenTitan mailbox, base 0x10404000) and the
 * seam this implements.
 *
 * This replaces the earlier Carfield-specific implementation (id*stride
 * multi-mailbox scheme, hardcoded IRQ 58, never confirmed against real
 * hardware). AlSaqr's map comes from a generated device-tree node
 * (alsaqr-fpga-ecs/dts/generate_dts.py) that titanssl_driver/driver.c
 * already runs against in production -- see docs/TITANSSL_ANALYSIS.md.
 */

static int real_mbox;
module_param(real_mbox, int, 0444);
MODULE_PARM_DESC(real_mbox, "Enable the real mailbox hardware backend (0=off, default). Mutually exclusive with mock_ot=1.");

static void __iomem *mbox_base;
static int mbox_irq;

static wait_queue_head_t mbox_wq;
static atomic_t mbox_completed;
static u32 mbox_reply_word0;
static u32 mbox_reply_word1;

static bool mbox_irq_requested;
static bool mbox_probed;
static bool mbox_driver_registered;

/* ── Device tree match / IRQ acquisition ─────────────────────────────────
 *
 * Same shape as titanssl_driver/driver.c's ot_mbox_probe(): the mailbox's
 * physical base address is a fixed constant (ALSAQR_MBOX_BASE_ADDR,
 * confirmed real, see header), but the IRQ number is read from the DT node
 * rather than hardcoded -- docs/TITANSSL_ANALYSIS.md §5 "RESERVE" flagged
 * this as the pattern to adopt once a real DT source exists; the generated
 * ot_mbox@10404000 node is that source now.
 */
static const struct of_device_id alsaqr_mbox_dt_ids[] = {
	{ .compatible = ALSAQR_MBOX_DT_COMPATIBLE, },
	{ }
};
MODULE_DEVICE_TABLE(of, alsaqr_mbox_dt_ids);

static int alsaqr_mbox_probe(struct platform_device *pdev)
{
	int irq = platform_get_irq(pdev, 0);

	if (irq <= 0) {
		pr_err("alsaqr_mbox_hw: probe: no IRQ resource on matched device (%d)\n", irq);
		return irq < 0 ? irq : -EINVAL;
	}

	mbox_irq = irq;
	mbox_probed = true;
	pr_info("alsaqr_mbox_hw: probe: matched %s, irq=%d\n",
		ALSAQR_MBOX_DT_COMPATIBLE, mbox_irq);
	return 0;
}

static int alsaqr_mbox_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver alsaqr_mbox_platform_driver = {
	.probe  = alsaqr_mbox_probe,
	.remove = alsaqr_mbox_remove,
	.driver = {
		.name           = "alsaqr-mbox-hw",
		.of_match_table = alsaqr_mbox_dt_ids,
	},
};

/* ── Register access ─────────────────────────────────────────────────────── */

static inline void __iomem *mbox_reg(unsigned long off)
{
	return mbox_base + off;
}

/* ── Outbound (host -> OT) ────────────────────────────────────────────────
 *
 * Wire format: only WORD0 (header_phys) and WORD1 (cmd) of the 5-word MBOX
 * area are used -- NOT titanssl's own 5-field ABI (magic/cmd+bitfield/
 * session/pid), which belongs to titanssl's firmware, not ours. We have no
 * OT firmware yet (see docs/QUESTIONS_FOR_TEAM.md), so this keeps the same
 * minimal header_phys+cmd contract the mock backend already uses.
 */

void alsaqr_mbox_hw_send(u32 header_phys, u32 cmd)
{
	if (!mbox_base) {
		pr_err("alsaqr_mbox_hw: send() called with no mapped hardware -- caller should have checked alsaqr_mbox_hw_enabled() first\n");
		return;
	}

	/*
	 * Reset before ringing the doorbell -- same "clear before releasing"
	 * discipline as alsaqr_mock_ot_send(). Without this, a reply that
	 * arrives late for an ABANDONED previous request (e.g. one that
	 * already timed out and was released) would leave completed=1 with
	 * stale reply words sitting here; the next send()'s wait_completion()
	 * would then return immediately with that stale reply instead of
	 * actually waiting for its own.
	 */
	atomic_set(&mbox_completed, 0);

	writel(header_phys, mbox_reg(ALSAQR_MBOX_REG_WORD0));
	writel(cmd,          mbox_reg(ALSAQR_MBOX_REG_WORD1));

	/* Letters visible before the doorbell. Whether this needs to be more
	 * than a plain data fence (real cache-management op, not yet known --
	 * docs/QUESTIONS_FOR_TEAM.md item 4) is still open; wmb() is the same
	 * conservative choice already made for Carfield's own mailbox map. */
	wmb();

	writel(1, mbox_reg(ALSAQR_MBOX_REG_DOORBELL));
}

/* ── Inbound (OT -> host) ─────────────────────────────────────────────────
 *
 * Reply convention: our own, not observed in titanssl (whose ABI has no
 * symmetric reply-letters concept) -- OT is expected to place its reply
 * into the same WORD0/WORD1 before signaling completion, mirroring what
 * alsaqr_mock_ot.c already does. Needs firmware-side agreement once Tina's
 * OT firmware exists; tracked as open alongside the other unconfirmed items
 * in docs/QUESTIONS_FOR_TEAM.md.
 */

int alsaqr_mbox_hw_wait_completion(long timeout_ms)
{
	long left = wait_event_interruptible_timeout(mbox_wq,
			atomic_read(&mbox_completed),
			msecs_to_jiffies(timeout_ms));

	if (left < 0)
		return -ERESTARTSYS;
	if (left == 0)
		return -ETIMEDOUT;

	atomic_set(&mbox_completed, 0);
	return 0;
}

void alsaqr_mbox_hw_read_reply(u32 *letter0, u32 *letter1)
{
	*letter0 = mbox_reply_word0;
	*letter1 = mbox_reply_word1;
}

/*
 * IRQ handler. Single dedicated PLIC source per the DT node (interrupts =
 * <10>, not shared with anything else per the generated DT) -- unlike the
 * old Carfield map, there is no second inbound mailbox id to demux against,
 * so this is requested without IRQF_SHARED.
 *
 * Completion is reset here (not later in wait_completion()'s caller) on the
 * same defensive assumption the old Carfield ISR used: if the line is
 * level-sensitive, leaving it unacked until userspace gets around to it
 * would wedge the interrupt. Reply words are read here too, before a
 * hypothetical next request could overwrite them.
 */
static irqreturn_t alsaqr_mbox_hw_irq(int irq, void *dev_id)
{
	mbox_reply_word0 = readl(mbox_reg(ALSAQR_MBOX_REG_WORD0));
	mbox_reply_word1 = readl(mbox_reg(ALSAQR_MBOX_REG_WORD1));

	writel(0, mbox_reg(ALSAQR_MBOX_REG_COMPLETION));

	atomic_set(&mbox_completed, 1);
	wake_up_interruptible(&mbox_wq);

	return IRQ_HANDLED;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

bool alsaqr_mbox_hw_requested(void)
{
	return real_mbox != 0;
}

bool alsaqr_mbox_hw_enabled(void)
{
	return real_mbox != 0 && mbox_base != NULL && mbox_irq_requested;
}

int alsaqr_mbox_hw_start(void)
{
	int ret;

	if (!real_mbox)
		return 0; /* no behavior change when real_mbox=0 */

	init_waitqueue_head(&mbox_wq);
	atomic_set(&mbox_completed, 0);

	ret = platform_driver_register(&alsaqr_mbox_platform_driver);
	if (ret) {
		pr_warn("alsaqr_mbox_hw: platform_driver_register failed: %d\n", ret);
		return 0; /* non-fatal, same tolerance as the rest of alsaqr_init() */
	}
	mbox_driver_registered = true;

	if (!mbox_probed) {
		pr_warn("alsaqr_mbox_hw: no matching '%s' device in the device tree -- real hardware not present (QEMU/x86 test rig?), backend stays disabled\n",
			ALSAQR_MBOX_DT_COMPATIBLE);
		return 0;
	}

	mbox_base = ioremap(ALSAQR_MBOX_BASE_ADDR, ALSAQR_MBOX_UNIT_SIZE);
	if (!mbox_base) {
		pr_warn("alsaqr_mbox_hw: ioremap mailbox region failed\n");
		return 0;
	}

	ret = request_irq(mbox_irq, alsaqr_mbox_hw_irq, 0,
			   "alsaqr-mbox-hw", NULL);
	if (ret) {
		pr_warn("alsaqr_mbox_hw: request_irq(%d) failed: %d\n",
			mbox_irq, ret);
		iounmap(mbox_base);
		mbox_base = NULL;
		return 0;
	}

	mbox_irq_requested = true;
	pr_info("alsaqr_mbox_hw: real mailbox backend started (base=0x%lx, irq=%d)\n",
		(unsigned long)ALSAQR_MBOX_BASE_ADDR, mbox_irq);
	return 0;
}

void alsaqr_mbox_hw_stop(void)
{
	if (mbox_irq_requested) {
		free_irq(mbox_irq, NULL);
		mbox_irq_requested = false;
	}

	if (mbox_base) {
		iounmap(mbox_base);
		mbox_base = NULL;
	}

	if (mbox_driver_registered) {
		platform_driver_unregister(&alsaqr_mbox_platform_driver);
		mbox_driver_registered = false;
	}
}
