#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

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

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x23c32d6f, "alloc_netdev_mqs" },
	{ 0xa28684c3, "register_netdev" },
	{ 0x4fffca41, "free_netdev" },
	{ 0x92997ed8, "_printk" },
	{ 0x56962cfc, "ether_setup" },
	{ 0x972a3723, "consume_skb" },
	{ 0x81873d75, "unregister_netdev" },
	{ 0x8631c9ba, "alt_cb_patch_nops" },
	{ 0xe28e8a80, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "74686ECAD2846552B37907D");
