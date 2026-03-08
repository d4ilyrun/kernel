TARGET  ?= $(ARCH)-dailyrun
HOST    ?= $(shell uname -m)-linux
SYSROOT ?= $(PWD)/$(BUILD_DIR)/$(ROOT_DIR)
PREFIX  ?= $(SYSROOT)/usr

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PATH)

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
include $(TOOLCHAIN_DIR)/newlib/build.mk
