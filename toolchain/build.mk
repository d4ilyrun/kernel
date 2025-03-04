TOOLCHAIN_LOCATION_DIR := $(TOOLCHAIN_DIR)/opt
TOOLCHAIN_BUILD_DIR := $(TOOLCHAIN_DIR)/build

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
