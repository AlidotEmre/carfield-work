#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_MITIGATION_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x6a7b86fa, "cdev_add" },
	{ 0x31ba63ba, "device_create" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x122c3a7e, "_printk" },
	{ 0xeea0e0d, "class_destroy" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x67d01ca4, "cdev_del" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xe9b868f, "device_destroy" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xa4bf0f83, "class_create" },
	{ 0x81daace6, "cdev_init" },
	{ 0x73776b79, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "3C9B9ABCA954607F409FE52");
