TOOLCHAIN_GCC_DIR  := $(TOOLCHAIN_DIR)/gcc

GCC_VERSION   := 12.3.0
GCC_TAR_URL   := https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/gcc-$(GCC_VERSION).tar.gz
GCC_TAR       := $(TOOLCHAIN_GCC_DIR)/gcc-$(GCC_VERSION).tar.gz
GCC_DIR       := $(TOOLCHAIN_GCC_DIR)/gcc-$(GCC_VERSION)
GCC_BUILD_DIR := $(BUILD_DIR)/gcc-$(GCC_VERSION)/$(TARGET)
GCC_GCC       := $(TOOLCHAIN_LOCATION)/bin/$(TARGET)-gcc

$(GCC_TAR):
	$(call COMPILE,WGET,$@)
	$(SILENT)$(WGET) $(GCC_TAR_URL) -O $@

gcc/prepare: $(GCC_DIR)
$(GCC_DIR): $(GCC_TAR)
	$(call COMPILE,EXTRACT,$@)
	$(SILENT)tar xf $(GCC_TAR) -C $(dir $@)
	$(call LOG,PATCH,$@)
	$(SILENT)cp -rf $(TOOLCHAIN_GCC_DIR)/target/* $@

gcc/configure: $(GCC_BUILD_DIR)/config.status
$(GCC_BUILD_DIR)/config.status: binutils $(GCC_DIR)
	$(call COMPILE,CONFIGURE,$@)
	$(SILENT)\
		cd $(dir $@) && \
		$(PWD)/$(GCC_DIR)/configure \
			--disable-nls --enable-languages=c \
			--with-sysroot=$(PWD)/$(TOOLCHAIN_SYSROOT) \
			--host="$(HOST)" \
			--target="$(TARGET)" \
			--prefix="$(PREFIX)" \
			$(GCC_CONFIGURE_FLAGS) \
		>  configure.log \
		2> configure.err

gcc: $(GCC_GCC)
$(GCC_GCC): install-headers gcc/configure
	$(call LOG,MAKE,gcc-$(GCC_VERSION)/$(TARGET))
	$(call MAKE_RECURSIVE,$(GCC_BUILD_DIR),all-gcc,gcc/)
	$(call MAKE_RECURSIVE,$(GCC_BUILD_DIR),all-target-libgcc,gcc/)
	$(call MAKE_RECURSIVE,$(GCC_BUILD_DIR),install-gcc,gcc/)
	$(call MAKE_RECURSIVE,$(GCC_BUILD_DIR),install-target-libgcc,gcc/)

.PHONY: gcc gcc/configure gcc/prepare

TO_DISTCLEAN += $(GCC_BUILD_DIR) $(GCC_DIR)
