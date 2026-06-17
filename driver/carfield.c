#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include "carfield.h"

#define DEVICE_NAME "carfield"
#define CLASS_NAME  "carfield"

struct carfield_dev {
	struct cdev          cdev;
	wait_queue_head_t    wq;
	int                  wq_flag;
};

static dev_t          dev_num;
static struct class  *carfield_class;
static struct carfield_dev cdev_data;

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

static long carfield_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct carfield_ping ping;

	if (_IOC_TYPE(cmd) != CARFIELD_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case CARFIELD_PING:
		if (copy_from_user(&ping, (void __user *)arg, sizeof(ping)))
			return -EFAULT;

		pr_info("carfield: ping value=%u\n", ping.value);
		ping.echo = ping.value;

		if (copy_to_user((void __user *)arg, &ping, sizeof(ping)))
			return -EFAULT;
		break;

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
};

static int __init carfield_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("carfield: alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	carfield_class = class_create(CLASS_NAME);
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
MODULE_DESCRIPTION("Carfield SoC kernel driver - Phase 0");
