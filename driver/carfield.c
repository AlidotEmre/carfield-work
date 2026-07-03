#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include "carfield.h"
#include "carfield_paging.h"

#define DEVICE_NAME "carfield"
#define CLASS_NAME  "carfield"

/* ── Physical memory map (from car_params.h + car_memory_map.h) ─────────── */
#define SOC_CTRL_PHYS        0x20010000UL
#define SOC_CTRL_SIZE        0x1000

#define INT_CLUSTER_PHYS     0x50000000UL
#define INT_CLUSTER_SIZE     0x800000

#define L2_INTL_0_PHYS       0x78000000UL
#define L2_INTL_0_SIZE       0x100000
#define L2_CONT_0_PHYS       0x78100000UL
#define L2_CONT_0_SIZE       0x100000
#define L2_INTL_1_PHYS       0x78200000UL
#define L2_INTL_1_SIZE       0x100000
#define L2_CONT_1_PHYS       0x78300000UL
#define L2_CONT_1_SIZE       0x100000

#define SAFETY_ISLAND_PHYS   0x60000000UL
#define SAFETY_ISLAND_SIZE   0x800000

#define SPATZ_CLUSTER_PHYS   0x51000000UL
#define SPATZ_CLUSTER_SIZE   0x800000

/* ── PULP cluster control register offsets (relative to soc_ctrl base) ─── */
/* from carfield/sw/include/regs/soc_ctrl.h */
#define PULP_FETCH_ENABLE_OFF  0xC0
#define PULP_BOOT_ENABLE_OFF   0xDC
#define PULP_BUSY_OFF          0xE4
#define PULP_EOC_OFF           0xE8

/* Boot address registers inside the cluster peripheral */
/* car_integer_cluster + 0x200000 (periph) + 0x000000 (ctrl unit) + 0x40 */
#define INT_CLUSTER_BOOT_ADDR_OFF  0x200040
/* Return value from cluster */
#define INT_CLUSTER_RETURN_OFF     0x200100

#define INT_CLUSTER_NUM_CORES      12

/*
 * PULP cluster's eoc_o signal reaches the host (Cheshire) as intr_ext_i[0]
 * ("pulpcl_eoc", level-sensitive) per the Carfield/Cheshire architecture
 * docs. The concrete PLIC source ID that maps to on Linux is NOT known yet
 * -- it depends on Cheshire's internal+external interrupt concatenation
 * order, which isn't in the public docs (needs the generated hardware
 * headers or confirmation from Daniele, same as the mailbox topology).
 *
 * 0 is not a valid IRQ number, so request_irq() below fails safely and
 * non-fatally (same tolerance as the ioremap regions above) until this
 * is filled in with the real PLIC source ID.
 */
#define CARFIELD_EOC_IRQ           0

/* EOC wait timeout, replaces the old busy-poll iteration count */
#define CARFIELD_EOC_TIMEOUT_MS    5000

/* ── Driver state ────────────────────────────────────────────────────────── */
struct carfield_dev {
	struct cdev          cdev;
	wait_queue_head_t    wq;
	int                  wq_flag;
};

static dev_t          dev_num;
static struct class  *carfield_class;
static struct carfield_dev cdev_data;

/* ioremap'd pointers — NULL if hardware not present */
static void __iomem  *soc_ctrl;
static void __iomem  *int_cluster;

/* set once request_irq(CARFIELD_EOC_IRQ, ...) succeeds, so carfield_exit()
 * knows whether there is anything to free_irq() */
static bool eoc_irq_requested;

/* ── mmap region table ───────────────────────────────────────────────────── */
struct mmap_region {
	unsigned long pgoff;
	phys_addr_t   phys;
	size_t        size;
};

static const struct mmap_region mmap_table[] = {
	{ CARFIELD_MMAP_SOC_CTRL,      SOC_CTRL_PHYS,      SOC_CTRL_SIZE      },
	{ CARFIELD_MMAP_L2_INTL_0,     L2_INTL_0_PHYS,     L2_INTL_0_SIZE     },
	{ CARFIELD_MMAP_L2_CONT_0,     L2_CONT_0_PHYS,     L2_CONT_0_SIZE     },
	{ CARFIELD_MMAP_L2_INTL_1,     L2_INTL_1_PHYS,     L2_INTL_1_SIZE     },
	{ CARFIELD_MMAP_L2_CONT_1,     L2_CONT_1_PHYS,     L2_CONT_1_SIZE     },
	{ CARFIELD_MMAP_SAFETY_ISLAND,  SAFETY_ISLAND_PHYS, SAFETY_ISLAND_SIZE },
	{ CARFIELD_MMAP_INT_CLUSTER,    INT_CLUSTER_PHYS,   INT_CLUSTER_SIZE   },
	{ CARFIELD_MMAP_SPATZ_CLUSTER,  SPATZ_CLUSTER_PHYS, SPATZ_CLUSTER_SIZE },
};

/* ── File operations ─────────────────────────────────────────────────────── */

static int carfield_open(struct inode *inode, struct file *file)
{
	file->private_data = container_of(inode->i_cdev,
					  struct carfield_dev, cdev);
	return 0;
}

static int carfield_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

/*
 * mmap: maps a hardware region into user space.
 * User space selects the region via the page offset (vm_pgoff).
 * Matches car_linux_mmap.h from the Carfield repo.
 */
static int carfield_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t req_size = vma->vm_end - vma->vm_start;
	phys_addr_t phys = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(mmap_table); i++) {
		if (mmap_table[i].pgoff == vma->vm_pgoff) {
			phys = mmap_table[i].phys;
			if (req_size > mmap_table[i].size)
				return -EINVAL;
			break;
		}
	}
	if (!phys)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start,
			       phys >> PAGE_SHIFT, req_size,
			       vma->vm_page_prot);
}

/*
 * EOC interrupt handler: PULP cluster signals end-of-computation, wakes up
 * whoever is blocked in the CARFIELD_CLUSTER_RUN ioctl's wait_event below.
 * Replaces the old busy-poll of PULP_EOC_OFF.
 */
static irqreturn_t carfield_eoc_isr(int irq, void *dev_id)
{
	struct carfield_dev *cdev = dev_id;

	cdev->wq_flag = 1;
	wake_up_interruptible(&cdev->wq);

	return IRQ_HANDLED;
}

static long carfield_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	if (_IOC_TYPE(cmd) != CARFIELD_MAGIC)
		return -ENOTTY;

	switch (cmd) {

	/* ── Phase 0: ping ────────────────────────────────────────────── */
	case CARFIELD_PING: {
		struct carfield_ping ping;

		if (copy_from_user(&ping, (void __user *)arg, sizeof(ping)))
			return -EFAULT;
		pr_info("carfield: ping value=%u\n", ping.value);
		ping.echo = ping.value;
		if (copy_to_user((void __user *)arg, &ping, sizeof(ping)))
			return -EFAULT;
		break;
	}

	/* ── Phase 2: boot PULP cluster and wait for EOC ─────────────── */
	case CARFIELD_CLUSTER_RUN: {
		struct carfield_cluster_run req;
		void __iomem *boot_reg;
		int i;
		long left;
		u32 num_cores;

		if (!soc_ctrl || !int_cluster) {
			pr_err("carfield: hardware regions not mapped\n");
			return -ENXIO;
		}

		if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
			return -EFAULT;

		num_cores = req.num_cores ? req.num_cores : INT_CLUSTER_NUM_CORES;
		if (num_cores > INT_CLUSTER_NUM_CORES) {
			pr_err("carfield: num_cores=%u exceeds INT_CLUSTER_NUM_CORES=%d\n",
			       num_cores, INT_CLUSTER_NUM_CORES);
			return -EINVAL;
		}

		/* Write boot address for each core */
		boot_reg = int_cluster + INT_CLUSTER_BOOT_ADDR_OFF;
		for (i = 0; i < num_cores; i++)
			writel(req.boot_addr, boot_reg + i * 4);

		/* Reset the flag before releasing the cluster so an EOC that
		 * fires between here and wait_event below isn't missed. */
		cdev_data.wq_flag = 0;

		/* Release cluster: assert BOOT_ENABLE then FETCH_ENABLE */
		writel(1, soc_ctrl + PULP_BOOT_ENABLE_OFF);
		writel(1, soc_ctrl + PULP_FETCH_ENABLE_OFF);

		/* Wait for the EOC interrupt (carfield_eoc_isr) instead of
		 * busy-polling PULP_EOC_OFF. */
		left = wait_event_interruptible_timeout(cdev_data.wq,
				cdev_data.wq_flag,
				msecs_to_jiffies(CARFIELD_EOC_TIMEOUT_MS));
		if (left < 0)
			return left; /* interrupted by a signal */
		if (left == 0) {
			pr_err("carfield: cluster EOC timeout\n");
			return -ETIMEDOUT;
		}

		req.result = readl(int_cluster + INT_CLUSTER_RETURN_OFF);

		if (copy_to_user((void __user *)arg, &req, sizeof(req)))
			return -EFAULT;
		break;
	}

	/* ── Paging chain test: pin/build/release, no mailbox/FPGA ─────── */
	case CARFIELD_PAGING_TEST: {
		struct carfield_paging_test_req req;
		struct carfield_paging_handle h;
		int ret;

		if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
			return -EFAULT;

		ret = carfield_paging_build(req.user_addr, req.user_size,
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

		carfield_paging_release(&h);

		if (copy_to_user((void __user *)arg, &req, sizeof(req)))
			return -EFAULT;
		break;
	}

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations carfield_fops = {
	.owner          = THIS_MODULE,
	.open           = carfield_open,
	.release        = carfield_release,
	.unlocked_ioctl = carfield_ioctl,
	.mmap           = carfield_mmap,
};

/* ── Module init / exit ──────────────────────────────────────────────────── */

static int __init carfield_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("carfield: alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	carfield_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(carfield_class)) {
		ret = PTR_ERR(carfield_class);
		goto err_chrdev;
	}

	cdev_init(&cdev_data.cdev, &carfield_fops);
	cdev_data.cdev.owner = THIS_MODULE;

	ret = cdev_add(&cdev_data.cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("carfield: cdev_add failed: %d\n", ret);
		goto err_class;
	}

	if (IS_ERR(device_create(carfield_class, NULL, dev_num,
				 NULL, DEVICE_NAME))) {
		ret = -ENOMEM;
		goto err_cdev;
	}

	init_waitqueue_head(&cdev_data.wq);
	cdev_data.wq_flag = 0;

	/* ioremap hardware regions — non-fatal if running without FPGA */
	soc_ctrl = ioremap(SOC_CTRL_PHYS, SOC_CTRL_SIZE);
	if (!soc_ctrl)
		pr_warn("carfield: ioremap soc_ctrl failed (no hardware?)\n");

	int_cluster = ioremap(INT_CLUSTER_PHYS, INT_CLUSTER_SIZE);
	if (!int_cluster)
		pr_warn("carfield: ioremap int_cluster failed (no hardware?)\n");

	/*
	 * CARFIELD_EOC_IRQ is a placeholder (see its definition) until the
	 * real PLIC source ID is known. 0 reliably fails as "invalid/busy"
	 * on the x86_64 test rig this has been validated on, but IRQ 0 is
	 * not universally invalid across architectures -- don't risk ever
	 * actually requesting a real line 0 on whatever the target platform
	 * turns out to be. Skip the call entirely instead.
	 */
	if (CARFIELD_EOC_IRQ > 0) {
		ret = request_irq(CARFIELD_EOC_IRQ, carfield_eoc_isr, 0,
				   DEVICE_NAME, &cdev_data);
		if (ret)
			pr_warn("carfield: request_irq(%d) failed: %d (EOC IRQ not wired up yet?)\n",
				CARFIELD_EOC_IRQ, ret);
		else
			eoc_irq_requested = true;
	} else {
		pr_warn("carfield: CARFIELD_EOC_IRQ not configured yet, skipping request_irq\n");
	}

	pr_info("carfield: /dev/%s ready (major=%d)\n",
		DEVICE_NAME, MAJOR(dev_num));
	return 0;

err_cdev:
	cdev_del(&cdev_data.cdev);
err_class:
	class_destroy(carfield_class);
err_chrdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit carfield_exit(void)
{
	if (eoc_irq_requested)
		free_irq(CARFIELD_EOC_IRQ, &cdev_data);

	if (soc_ctrl)
		iounmap(soc_ctrl);
	if (int_cluster)
		iounmap(int_cluster);

	device_destroy(carfield_class, dev_num);
	cdev_del(&cdev_data.cdev);
	class_destroy(carfield_class);
	unregister_chrdev_region(dev_num, 1);
	pr_info("carfield: removed\n");
}

module_init(carfield_init);
module_exit(carfield_exit);

MODULE_AUTHOR("AlidotEmre");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Carfield SoC kernel driver - Phase 3 (paging chain)");
