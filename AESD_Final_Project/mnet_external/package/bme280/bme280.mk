##############################################################
#
# BME280 Kernel Module (GitHub source)
#
##############################################################

BME280_VERSION = 8326d3a86655c5e66f45eac99740d8a433157836


BME280_SITE = git@github.com:KarthikRevoor/mnet-apps.git

BME280_SITE_METHOD = git

BME280_GIT_SUBMODULES = NO
BME280_MODULE_SUBDIRS = bme280/src


##############################################################
# Build the kernel module
##############################################################
define BME280_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D/bme280/src) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules
endef


##############################################################
# Install the kernel module on target rootfs
##############################################################
define BME280_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(@D)/bme280/src/bme280.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/kernel/drivers/misc/bme280.ko
endef


##############################################################
# Evaluate package type
##############################################################
$(eval $(kernel-module))
$(eval $(generic-package))

