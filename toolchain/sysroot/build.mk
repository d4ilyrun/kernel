TOOLCHAIN_SYSROOT_DIR  := $(TOOLCHAIN_DIR)/sysroot

# TODO: Install the actual libc headers instead of using a set of minimal ones
sysroot:
	$(call INSTALL, $(TOOLCHAIN_SYSROOT_DIR)/usr, $(SYSROOT))
	$(call INSTALL, $(INC_DIR)/uapi, $(SYSROOT)/usr/include)
