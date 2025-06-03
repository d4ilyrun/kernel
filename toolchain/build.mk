TOOLCHAIN_SYSROOT      := $(TOOLCHAIN_DIR)/sysroot
TOOLCHAIN_LOCATION     := $(TOOLCHAIN_DIR)/sysroot/usr

TARGET ?= $(ARCH)-$(NAME)
HOST   ?= $(shell uname -m)-linux
PREFIX ?= $(PWD)/$(TOOLCHAIN_LOCATION)

# Add manually built compiler to the path
export PATH := $(PREFIX)/bin:$(PREFIX)/usr/bin:$(PATH)

# Use our own toolchain to compile our kernel.
# This lets us be sure that the userland constants (syscall flags, ...) used
# by our kernel are the same as the one used to compile userland programs.
CROSS_COMPILE ?= $(TARGET)-

# Add the toolchain's include directory to the header search path so that
# it can be used by clangd.
CPPFLAGS      += -I$(PREFIX)/$(TARGET)/include -I$(PREFIX)/include

ifeq ($(ARCH),)
  $(info Target architecture: Undefined)
else
  $(info Target architecture: $(ARCH))
endif

$(info Toolchain: $(CROSS_COMPILE))

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
