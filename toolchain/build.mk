TARGET  ?= $(ARCH)-dailyrun
HOST    ?= $(shell uname -m)-linux
PREFIX  ?= $(PWD)/$(BUILD_DIR)/opt
SYSROOT ?= $(PWD)/$(BUILD_DIR)/sysroot

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PREFIX)/usr/bin:$(PATH)

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
include $(TOOLCHAIN_DIR)/newlib/build.mk
