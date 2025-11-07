BR2_EXTERNAL_MNET_EXTERNAL_PATH := $(dir $(lastword $(MAKEFILE_LIST)))

include $(sort $(wildcard $(BR2_EXTERNAL_MNET_EXTERNAL_PATH)/package/*/*.mk))
