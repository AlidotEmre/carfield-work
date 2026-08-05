#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include "alsaqr.h"
#include "alsaqr_paging.h"
#include "alsaqr_mock_ot.h"
#include "alsaqr_mbox_hw.h"

#define DEVICE_NAME "alsaqr"
#define CLASS_NAME  "alsaqr"

/* How long ALSAQR_OT_XFORM waits for the mock kthread's reply --
 * comfortably longer than any reasonable mock_delay_ms test value, so a
 * real -ETIMEDOUT only happens for mock_no_reply (or a wedged mock). */
#define ALSAQR_MOCK_OT_TIMEOUT_MS 2000

/* ── Driver state ────────────────────────────────────────────────────────── */
struct alsaqr_dev {
	struct cdev          cdev;
};

static dev_t          dev_num;
static struct class  *alsaqr_class;
static struct alsaqr_dev cdev_data;

/* ── File operations ─────────────────────────────────────────────────────── */

static int alsaqr_open(struct inode *inode, struct file *file)
{
	file->private_data = container_of(inode->i_cdev,
					  struct alsaqr_dev, cdev);
	return 0;
}

static int alsaqr_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long alsaqr_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	if (_IOC_TYPE(cmd) != ALSAQR_MAGIC)
		return -ENOTTY;

	switch (cmd) {

	/* ── Phase 0: ping ────────────────────────────────────────────── */
	case ALSAQR_PING: {
		struct alsaqr_ping ping;

		if (copy_from_user(&ping, (void __user *)arg, sizeof(ping)))
			return -EFAULT;
		pr_info("alsaqr: ping value=%u\n", ping.value);
		ping.echo = ping.value;
		if (copy_to_user((void __user *)arg, &ping, sizeof(ping)))
			return -EFAULT;
		break;
	}

	/* ── Paging chain test: pin/build/release, no mailbox/FPGA ─────── */
	case ALSAQR_PAGING_TEST: {
		struct alsaqr_paging_test_req req;
		struct alsaqr_paging_handle h;
		int ret;

		if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
			return -EFAULT;

		ret = alsaqr_paging_build(req.user_addr, req.user_size,
					     true, &h);
		if (ret)
			return ret;

		req.dsz          = h.info.data_size;
		req.nop          = h.info.nop;
		req.fpo          = h.info.fpo;
		req.fps          = h.info.fps;
		req.lps          = h.info.lps;
		req.header_phys  = h.header_phys;
		req.first_page_phys = h.map[0];
		req.last_page_phys  = h.map[h.info.nop - 1];

		alsaqr_paging_release(&h);

		if (copy_to_user((void __user *)arg, &req, sizeof(req)))
			return -EFAULT;
		break;
	}

	/* ── OpenTitan consumer, mock OR real hardware (MOCK_OT_SPEC.md) ──
	 *
	 * Same ioctl/struct for both backends -- mock_ot=1 and real_mbox=1
	 * are mutually exclusive (enforced in alsaqr_init()), so at most
	 * one of alsaqr_mock_ot_enabled()/alsaqr_mbox_hw_enabled() is
	 * ever true. The flow (build -> send -> wait -> read -> release) is
	 * unchanged from the mock-only version; only which three functions
	 * it calls differs.
	 *
	 * Renamed from ALSAQR_MOCK_OT_XFORM (2026-07-31): the old name
	 * baked "mock" into a struct/ioctl that was already carrying real
	 * hardware traffic whenever real_mbox=1 -- see
	 * memory/project_alsaqr.md for the rename. The mock kthread's own
	 * implementation (alsaqr_mock_ot.c: send/wait_completion/
	 * read_reply/status_to_errno, the ALSAQR_MOCK_OT_ERR_* status
	 * codes, the mock_* module params) correctly keeps "mock" in its
	 * name -- only the shared ioctl-facing symbols moved.
	 */
	case ALSAQR_OT_XFORM: {
		struct alsaqr_ot_xform_req req;
		struct alsaqr_paging_handle h;
		u32 letter0, letter1;
		bool use_hw = alsaqr_mbox_hw_enabled();
		int ret;

		if (!use_hw && !alsaqr_mock_ot_enabled())
			return -ENODEV;

		if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
			return -EFAULT;

		ret = alsaqr_paging_build(req.user_addr, req.user_size,
					     true, &h);
		if (ret)
			return ret;

		if (use_hw)
			alsaqr_mbox_hw_send(h.header_phys, ALSAQR_OT_CMD_XFORM);
		else
			alsaqr_mock_ot_send(h.header_phys, ALSAQR_OT_CMD_XFORM);

		ret = use_hw ? alsaqr_mbox_hw_wait_completion(ALSAQR_MOCK_OT_TIMEOUT_MS)
			     : alsaqr_mock_ot_wait_completion(ALSAQR_MOCK_OT_TIMEOUT_MS);
		if (ret) {
			/* mock_no_reply / real timeout, or a signal -- release
			 * pages either way so this never leaks, then report
			 * the failure. */
			alsaqr_paging_release(&h);
			req.ot_status = ALSAQR_OT_STATUS_NONE;
			if (copy_to_user((void __user *)arg, &req, sizeof(req)))
				return -EFAULT;
			return ret;
		}

		if (use_hw)
			alsaqr_mbox_hw_read_reply(&letter0, &letter1);
		else
			alsaqr_mock_ot_read_reply(&letter0, &letter1);
		(void)letter0; /* echoed header_phys, nothing to cross-check
				* it against here -- h is about to be released */
		alsaqr_paging_release(&h);

		req.ot_status = letter1;
		if (copy_to_user((void __user *)arg, &req, sizeof(req)))
			return -EFAULT;

		if (use_hw) {
			/*
			 * Real OpenTitan's letter1 status-code semantics are
			 * NOT known -- MOCK_OT_SPEC.md §5's OK/ERR_MAGIC/...
			 * table is a convention this driver's own mock
			 * invented for testing, not something confirmed
			 * against real firmware. Running a real status
			 * through alsaqr_mock_ot_status_to_errno() would
			 * silently mislabel it as one of the mock's own
			 * meanings. Report success-of-transport (a reply
			 * arrived) and hand the raw value to userspace via
			 * ot_status for now; revisit once Daniele's session
			 * clarifies real status codes.
			 */
			return 0;
		}
		return alsaqr_mock_ot_status_to_errno(letter1);
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations alsaqr_fops = {
	.owner          = THIS_MODULE,
	.open           = alsaqr_open,
	.release        = alsaqr_release,
	.unlocked_ioctl = alsaqr_ioctl,
};

/* ── Module init / exit ──────────────────────────────────────────────────── */

static int __init alsaqr_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("alsaqr: alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	alsaqr_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(alsaqr_class)) {
		ret = PTR_ERR(alsaqr_class);
		goto err_chrdev;
	}

	cdev_init(&cdev_data.cdev, &alsaqr_fops);
	cdev_data.cdev.owner = THIS_MODULE;

	ret = cdev_add(&cdev_data.cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("alsaqr: cdev_add failed: %d\n", ret);
		goto err_class;
	}

	if (IS_ERR(device_create(alsaqr_class, NULL, dev_num,
				 NULL, DEVICE_NAME))) {
		ret = -ENOMEM;
		goto err_cdev;
	}

	/*
	 * mock_ot=1 and real_mbox=1 are two backends for the same channel
	 * seam -- never start both. Their internal state is fully separate
	 * (no data race between them), but the ioctl's use_hw selection
	 * always prefers hardware when alsaqr_mbox_hw_enabled() is true,
	 * so if both were left running, mock_ot=1 would silently become dead
	 * weight (kthread spun up, CPU/memory held, never actually
	 * exercised) instead of testing what its caller asked for -- exactly
	 * the "never both backends live at once" spec requirement this
	 * guards. Refuse both, leave the rest of the driver
	 * (PING/PAGING_TEST) usable rather than failing the whole insmod.
	 */
	if (alsaqr_mock_ot_requested() && alsaqr_mbox_hw_requested()) {
		pr_err("alsaqr: mock_ot=1 and real_mbox=1 both set -- mutually exclusive backends, starting neither\n");
	} else {
		ret = alsaqr_mock_ot_start();
		if (ret)
			pr_warn("alsaqr: alsaqr_mock_ot_start failed: %d\n", ret);

		ret = alsaqr_mbox_hw_start();
		if (ret)
			pr_warn("alsaqr: alsaqr_mbox_hw_start failed: %d\n", ret);
	}

	pr_info("alsaqr: /dev/%s ready (major=%d)\n",
		DEVICE_NAME, MAJOR(dev_num));
	return 0;

err_cdev:
	cdev_del(&cdev_data.cdev);
err_class:
	class_destroy(alsaqr_class);
err_chrdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit alsaqr_exit(void)
{
	alsaqr_mock_ot_stop();
	alsaqr_mbox_hw_stop();

	device_destroy(alsaqr_class, dev_num);
	cdev_del(&cdev_data.cdev);
	class_destroy(alsaqr_class);
	unregister_chrdev_region(dev_num, 1);
	pr_info("alsaqr: removed\n");
}

module_init(alsaqr_init);
module_exit(alsaqr_exit);

MODULE_AUTHOR("AlidotEmre");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Alsaqr SoC kernel driver - Phase 3 (paging chain)");
