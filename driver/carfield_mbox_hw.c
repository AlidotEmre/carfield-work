#include "carfield_mbox_hw.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/printk.h>
#include <linux/errno.h>

/*
 * Real hardware backend -- see carfield_mbox_hw.h for the register map and
 * the seam this implements. Ported from
 * mailbox-simulation-withoutFPGA/carfield_mbox.c (carfield_mbox_sim repo,
 * commit e5708e6): that file is already written in kernel-API shape
 * (writel/readl/__iomem/wait_event_interruptible_timeout) and validated
 * end-to-end against a mock MMIO region in userspace. This is the same
 * logic against a real ioremap'd base instead of a malloc'd shim.
 */

static int real_mbox;
module_param(real_mbox, int, 0444);
MODULE_PARM_DESC(real_mbox, "Enable the real mailbox hardware backend (0=off, default). Mutually exclusive with mock_ot=1.");

static void __iomem *mbox_unit_base;

struct carfield_mbox_in {
	unsigned int       id;
	wait_queue_head_t  wq;
	atomic_t           completed;
	u32                letter0;
	u32                letter1;
};

static struct carfield_mbox_in mbox_in_pulp;	/* id 5, provisioned, unread */
static struct carfield_mbox_in mbox_in_ot;	/* id 7, the XFORM reply channel */

static bool mbox_irq_requested;

/* ── Register access ─────────────────────────────────────────────────────── */

static inline void __iomem *mbox_reg(unsigned int id, unsigned long off)
{
	return mbox_unit_base + carfield_mbox_reg_addr(id, off);
}

/* ── Outbound (host -> OT, mailbox id 1) ─────────────────────────────────── */

void carfield_mbox_hw_send(u32 header_phys, u32 cmd)
{
	if (!mbox_unit_base) {
		pr_err("carfield_mbox_hw: send() called with no mapped hardware -- caller should have checked carfield_mbox_hw_enabled() first\n");
		return;
	}

	/*
	 * Reset before ringing the doorbell -- same "clear before releasing"
	 * discipline as carfield_mock_ot_send(). Without this, a reply that
	 * arrives late for an ABANDONED previous request (e.g. one that
	 * already timed out and was released) would leave completed=1 with
	 * stale letter0/letter1 sitting in mbox_in_ot; the next send()'s
	 * wait_completion() would then return immediately with that stale
	 * reply instead of actually waiting for its own.
	 */
	atomic_set(&mbox_in_ot.completed, 0);

	writel(header_phys, mbox_reg(CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_LETTER0));
	writel(cmd,          mbox_reg(CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_LETTER1));

	/* Letters visible before the doorbell. Whether this needs to be more
	 * than a plain data fence (real cache-management op, not yet known --
	 * docs/QUESTIONS_FOR_TEAM.md item 4) is still open; wmb() is the same
	 * conservative choice already made in carfield_mbox_sim. */
	wmb();

	/*
	 * Doorbell is INT_SND_SET, not INT_RCV_SET, on ALL three directions
	 * (Daniele, 2026-07-09) -- this corrects the mbox.h he separately
	 * shared (2026-07-06), whose *active* mailbox_send() rings
	 * INT_RCV_SET instead. That RCV_SET write is dead code on Daniele's
	 * own confirmation (no interrupt line is wired to it); do not port
	 * it. See docs/QUESTIONS_FOR_TEAM.md item 1 and
	 * memory/project_alsaqr.md "Daniele'nin Yeni Cevapları" section.
	 */
	writel(1, mbox_reg(CARFIELD_MBOX_ID_HOST_TO_OT, CARFIELD_MBOX_REG_SND_SET));
}

/* ── Inbound (OT -> host, mailbox id 7 = the XFORM reply channel) ───────── */

int carfield_mbox_hw_wait_completion(long timeout_ms)
{
	long left = wait_event_interruptible_timeout(mbox_in_ot.wq,
			atomic_read(&mbox_in_ot.completed),
			msecs_to_jiffies(timeout_ms));

	if (left < 0)
		return -ERESTARTSYS;
	if (left == 0)
		return -ETIMEDOUT;

	atomic_set(&mbox_in_ot.completed, 0);
	return 0;
}

void carfield_mbox_hw_read_reply(u32 *letter0, u32 *letter1)
{
	*letter0 = mbox_in_ot.letter0;
	*letter1 = mbox_in_ot.letter1;
}

/*
 * Shared IRQ handler, one instance per inbound mailbox (registered twice,
 * once with &mbox_in_pulp and once with &mbox_in_ot as dev_id -- both
 * mailbox 5 and mailbox 7 funnel into the same host IRQ per the spec this
 * was written against). The STAT check is the demux: if this mailbox's
 * line isn't asserted, IRQ_NONE says "not mine, keep looking" to the other
 * shared-IRQ handler. Level-sensitive, so CLR must happen immediately or
 * the line (and the whole shared IRQ) stays wedged.
 *
 * Letters are read here, under the handler, so a second doorbell can't
 * overwrite them before wait_completion()'s caller gets a chance to read
 * via carfield_mbox_hw_read_reply() -- mirrors carfield_mock_ot's
 * "channel holds exactly one in-flight reply" contract.
 */
static irqreturn_t carfield_mbox_hw_irq(int irq, void *dev_id)
{
	struct carfield_mbox_in *mb = dev_id;

	if (!readl(mbox_reg(mb->id, CARFIELD_MBOX_REG_SND_STAT)))
		return IRQ_NONE;

	writel(1, mbox_reg(mb->id, CARFIELD_MBOX_REG_SND_CLR));

	mb->letter0 = readl(mbox_reg(mb->id, CARFIELD_MBOX_REG_LETTER0));
	mb->letter1 = readl(mbox_reg(mb->id, CARFIELD_MBOX_REG_LETTER1));

	atomic_set(&mb->completed, 1);
	wake_up_interruptible(&mb->wq);

	return IRQ_HANDLED;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

bool carfield_mbox_hw_requested(void)
{
	return real_mbox != 0;
}

bool carfield_mbox_hw_enabled(void)
{
	return real_mbox != 0 && mbox_unit_base != NULL;
}

int carfield_mbox_hw_start(void)
{
	int ret;

	if (!real_mbox)
		return 0; /* no behavior change when real_mbox=0 */

	init_waitqueue_head(&mbox_in_pulp.wq);
	atomic_set(&mbox_in_pulp.completed, 0);
	mbox_in_pulp.id = CARFIELD_MBOX_ID_PULP_TO_HOST;

	init_waitqueue_head(&mbox_in_ot.wq);
	atomic_set(&mbox_in_ot.completed, 0);
	mbox_in_ot.id = CARFIELD_MBOX_ID_OT_TO_HOST;

	/*
	 * CARFIELD_MBOX_UNIT_SIZE covers ids 0..7 by the confirmed
	 * base+id*stride formula -- NOT a confirmed total size of the real
	 * mailbox IP block. If the real unit's address span differs, this is
	 * the one place to fix (see the header comment on the define).
	 * Non-fatal on failure, same tolerance as soc_ctrl/int_cluster in
	 * carfield.c -- carfield_mbox_hw_enabled() will correctly report
	 * "not ready" and the ioctl returns -ENODEV instead of crashing.
	 */
	mbox_unit_base = ioremap(CARFIELD_MBOX_BASE_ADDR, CARFIELD_MBOX_UNIT_SIZE);
	if (!mbox_unit_base) {
		pr_warn("carfield_mbox_hw: ioremap mailbox unit failed (no hardware?)\n");
		return 0;
	}

	/*
	 * CARFIELD_MBOX_IRQ is now a confirmed value (58, see its definition
	 * for sourcing) -- this branch is live on real hardware, not the
	 * dead/skipped placeholder path CARFIELD_EOC_IRQ in carfield.c still
	 * is. The `> 0` guard is kept as defense-in-depth (harmless if the
	 * define is ever reverted to a placeholder again) rather than
	 * removed outright.
	 *
	 * INT_SND_EN is deliberately written AFTER request_irq() succeeds for
	 * each mailbox, not before: enabling the interrupt line at the
	 * mailbox IP with no request_irq()'d handler behind it would be
	 * pointless (nothing will service it -- STAT is still pollable
	 * without EN per the register-map doc comment), and ordering it this
	 * way means a request_irq() failure (e.g. IRQ 58 already claimed by
	 * something else on the real platform) leaves the source disabled
	 * rather than enabled-but-unhandled. Mirrors
	 * carfield_mbox_sim/carfield_mbox.c's carfield_mbox_in_init() intent
	 * (receiver enables its own INT_SND_EN; still unconfirmed which side
	 * is supposed to own this write, docs/QUESTIONS_FOR_TEAM.md item 1)
	 * but reordered relative to IRQ registration for that reason.
	 */
	if (CARFIELD_MBOX_IRQ > 0) {
		ret = request_irq(CARFIELD_MBOX_IRQ, carfield_mbox_hw_irq,
				   IRQF_SHARED, "carfield-mbox-pulp", &mbox_in_pulp);
		if (ret) {
			pr_warn("carfield_mbox_hw: request_irq(%d) failed for mailbox %d: %d\n",
				CARFIELD_MBOX_IRQ, CARFIELD_MBOX_ID_PULP_TO_HOST, ret);
		} else {
			ret = request_irq(CARFIELD_MBOX_IRQ, carfield_mbox_hw_irq,
					   IRQF_SHARED, "carfield-mbox-ot", &mbox_in_ot);
			if (ret) {
				pr_warn("carfield_mbox_hw: request_irq(%d) failed for mailbox %d: %d\n",
					CARFIELD_MBOX_IRQ, CARFIELD_MBOX_ID_OT_TO_HOST, ret);
				free_irq(CARFIELD_MBOX_IRQ, &mbox_in_pulp);
			} else {
				mbox_irq_requested = true;
				writel(1, mbox_reg(CARFIELD_MBOX_ID_PULP_TO_HOST, CARFIELD_MBOX_REG_SND_EN));
				writel(1, mbox_reg(CARFIELD_MBOX_ID_OT_TO_HOST,   CARFIELD_MBOX_REG_SND_EN));
			}
		}
	} else {
		pr_warn("carfield_mbox_hw: CARFIELD_MBOX_IRQ not configured yet, skipping request_irq (and INT_SND_EN)\n");
	}

	pr_info("carfield_mbox_hw: real mailbox backend started (base=0x%lx)\n",
		(unsigned long)CARFIELD_MBOX_BASE_ADDR);
	return 0;
}

void carfield_mbox_hw_stop(void)
{
	if (mbox_irq_requested) {
		free_irq(CARFIELD_MBOX_IRQ, &mbox_in_ot);
		free_irq(CARFIELD_MBOX_IRQ, &mbox_in_pulp);
		mbox_irq_requested = false;
	}

	if (mbox_unit_base) {
		iounmap(mbox_unit_base);
		mbox_unit_base = NULL;
	}
}
