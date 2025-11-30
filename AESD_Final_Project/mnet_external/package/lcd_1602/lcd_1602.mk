
################################################################################
#
# lcd_1602 userspace app
#
################################################################################

# Commit in mnet-apps that contains display/src/lcd_1602.c
LCD_1602_VERSION = a8e9dbc4d4f00e216eb14bd36c61f5bbea7e3a0d
LCD_1602_SITE    = git@github.com:KarthikRevoor/mnet-apps.git
LCD_1602_SITE_METHOD = git

# Subdirectory inside the mnet-apps repo
LCD_1602_SUBDIR = display/src

define LCD_1602_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		-I$(@D)/$(LCD_1602_SUBDIR) \
		$(@D)/$(LCD_1602_SUBDIR)/lcd1602_i2c.c \
		-o $(@D)/$(LCD_1602_SUBDIR)/lcd_1602 \
		$(TARGET_LDFLAGS)
endef

define LCD_1602_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 \
		$(@D)/$(LCD_1602_SUBDIR)/lcd_1602 \
		$(TARGET_DIR)/usr/bin/lcd_1602
endef

$(eval $(generic-package))

