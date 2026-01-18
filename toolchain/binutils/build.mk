TOOLCHAIN_BINUTILS_DIR := $(TOOLCHAIN_DIR)/binutils

BINUTILS_VERSION   := 2.44
BINUTILS_TAR_URL   := https://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VERSION).tar.gz
BINUTILS_TAR       := $(TOOLCHAIN_BINUTILS_DIR)/binutils-$(BINUTILS_VERSION).tar.gz
BINUTILS_DIR       := $(TOOLCHAIN_BINUTILS_DIR)/binutils-$(BINUTILS_VERSION)
BINUTILS_BUILD_DIR := $(BUILD_DIR)/binutils-$(BINUTILS_VERSION)/$(TARGET)
BINUTILS_LD        := $(PREFIX)/bin/$(TARGET)-ld

$(BINUTILS_TAR):
	$(call COMPILE,WGET,$@)
	$(SILENT)$(WGET) $(BINUTILS_TAR_URL) -O $@

binutils/prepare: $(BINUTILS_DIR)
$(BINUTILS_DIR): $(BINUTILS_TAR)
	$(call COMPILE,EXTRACT,$@)
	$(SILENT)tar xf $(BINUTILS_TAR) -C $(dir $@)
	$(call LOG,PATCH,$@)
	$(SILENT)cp -rf $(TOOLCHAIN_BINUTILS_DIR)/target/* $@

binutils/configure: $(BINUTILS_BUILD_DIR)/config.status
$(BINUTILS_BUILD_DIR)/config.status: $(BINUTILS_DIR)
	$(call COMPILE,CONFIGURE,$@)
	$(SILENT)\
		cd $(dir $@) && \
		$(PWD)/$(BINUTILS_DIR)/configure \
			--disable-nls --disable-werror \
			--with-sysroot=$(SYSROOT) \
			--host="$(HOST)" \
			--target="$(TARGET)" \
			--prefix="$(PREFIX)" \
			$(BINUTILS_CONFIGURE_FLAGS) \
		>  configure.log \
		2> configure.err

# Target an arbitrary binutil to avoid recompiling everything
binutils: $(BINUTILS_LD)
$(BINUTILS_LD): binutils/configure
	$(call LOG,MAKE,binutils-$(BINUTILS_VERSION)/$(TARGET))
	$(call MAKE_RECURSIVE,$(BINUTILS_BUILD_DIR),all,binutils/)
	$(call MAKE_RECURSIVE,$(BINUTILS_BUILD_DIR),install,binutils/)

.PHONY: binutils binutils/configure binutils/prepare

TO_DISTCLEAN += $(BINUTILS_BUILD_DIR) $(BINUTILS_DIR)
