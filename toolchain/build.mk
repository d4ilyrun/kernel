TOOLCHAIN_LOCATION     := $(TOOLCHAIN_DIR)/opt
TOOLCHAIN_SYSROOT      := $(TOOLCHAIN_DIR)/sysroot

TARGET ?= $(ARCH)-dailyrun
HOST   ?= $(shell uname -m)-linux
PREFIX ?= $(PWD)/$(TOOLCHAIN_LOCATION)

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PREFIX)/usr/bin:$(PATH)

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
include $(TOOLCHAIN_DIR)/newlib/build.mk

TO_DISTCLEAN += $(TOOLCHAIN_LOCATION)
