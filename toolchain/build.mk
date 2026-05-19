TARGET  ?= $(ARCH)-dailyrun
HOST    ?= $(shell uname -m)-linux
PREFIX  ?= $(PWD)/$(BUILD_DIR)/toolchain/$(TARGET)

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PATH)

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
include $(TOOLCHAIN_DIR)/newlib/build.mk
