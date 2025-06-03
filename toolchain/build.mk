TOOLCHAIN_SYSROOT      := $(TOOLCHAIN_DIR)/sysroot
TOOLCHAIN_LOCATION     := $(TOOLCHAIN_DIR)/sysroot/usr

TARGET ?= $(ARCH)-$(NAME)
HOST   ?= $(shell uname -m)-linux
PREFIX ?= $(PWD)/$(TOOLCHAIN_LOCATION)

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PREFIX)/usr/bin:$(PATH)

include $(TOOLCHAIN_DIR)/binutils/build.mk
include $(TOOLCHAIN_DIR)/gcc/build.mk
include $(TOOLCHAIN_DIR)/newlib/build.mk

install-headers:
	$(call LOG,GEN,sysroot)
	$(SILENT)mkdir -p $(PREFIX)/$(TARGET)/include
	$(SILENT)mkdir -p $(PREFIX)/$(TARGET)/lib
	$(SILENT)[ -L $(PREFIX)/include ] || ln -s $(TARGET)/include $(PREFIX)/include
	$(SILENT)[ -L $(PREFIX)/lib ] || ln -s $(TARGET)/lib $(PREFIX)/lib
	$(call LOG,COPY,include/uapi)
	$(SILENT)cp -rf $(INC_DIR)/uapi $(PREFIX)/include/$(NAME)

.PHONY: install-headers

TO_DISTCLEAN += $(TOOLCHAIN_SYSROOT)
