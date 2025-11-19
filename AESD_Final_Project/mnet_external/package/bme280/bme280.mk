################################################################################
# bme280 kernel module package
################################################################################

BME280_VERSION = 1.0
BME280_SITE = $(BR2_EXTERNAL_MNET_EXTERNAL_PATH)/package/bme280/src
BME280_SITE_METHOD = local

define BME280_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules
endef

define BME280_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(@D)/bme280.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/kernel/drivers/misc/bme280.ko
endef

$(eval $(kernel-module))
$(eval $(generic-package))

