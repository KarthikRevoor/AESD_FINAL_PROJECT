##############################################################
#
# BME280 Kernel Module (GitHub source)
#
##############################################################

# Commit hash you want Buildroot to checkout
BME280_VERSION = a7c91b92e34f3b4e6e7f93c75ff128c322ab1234

# GitHub SSH URL (recommended so Buildroot uses your SSH key)
# MUST be ssh format when private repo or when using CI
BME280_SITE = git@github.com:<username>/<repo>.git

BME280_SITE_METHOD = git
# Set to YES only if repo contains submodules
BME280_GIT_SUBMODULES = NO


##############################################################
# Build the kernel module
##############################################################
define BME280_BUILD_CMDS
	$(MAKE) -C $(LINUX_DIR) \
		M=$(@D) \
		ARCH=$(KERNEL_ARCH) \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		modules
endef


##############################################################
# Install the kernel module on target rootfs
##############################################################
define BME280_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 644 $(@D)/bme280.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/kernel/drivers/misc/bme280.ko
endef


##############################################################
# Evaluate package type
##############################################################
$(eval $(kernel-module))
$(eval $(generic-package))

