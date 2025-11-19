################################################################################
# mnet package description
################################################################################

MNET_VERSION = 1.0
MNET_SITE = $(BR2_EXTERNAL_MNET_EXTERNAL_PATH)/package/mnet
MNET_SITE_METHOD = local

define MNET_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules
endef

define MNET_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(@D)/src/mnet.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/kernel/drivers/net/mnet.ko
endef

$(eval $(kernel-module))
$(eval $(generic-package))
