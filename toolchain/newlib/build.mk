TOOLCHAIN_NEWLIB_DIR := $(TOOLCHAIN_DIR)/newlib

NEWLIB_VERSION		:= 4.5.0.20241231
NEWLIB_TAR_URL		:= ftp://sourceware.org/pub/newlib/newlib-$(NEWLIB_VERSION).tar.gz
NEWLIB_DIR			:= $(TOOLCHAIN_NEWLIB_DIR)/newlib-$(NEWLIB_VERSION)
NEWLIB_TAR			:= $(NEWLIB_DIR).tar.gz
NEWLIB_BUILD_DIR	:= $(BUILD_DIR)/newlib-$(NEWLIB_VERSION)

newlib/tar: $(NEWLIB_TAR)
$(NEWLIB_TAR):
	$(call COMPILE,WGET,$@)
	$(SILENT)$(WGET) $(NEWLIB_TAR_URL) -O $@

newlib/prepare: $(NEWLIB_DIR)
$(NEWLIB_DIR): $(NEWLIB_TAR)
	$(SILENT)$(call CHECK_VERSION,autoconf,2.69)
	$(SILENT)$(call CHECK_VERSION,automake,1.15.1)
	$(call COMPILE,EXTRACT,$@)
	$(SILENT)tar xf $(NEWLIB_TAR) -C $(dir $@)
	$(call LOG,PATCH,$@)
	$(SILENT)cp -rf $(TOOLCHAIN_NEWLIB_DIR)/port/* $@
	$(call LOG,RECONF,$@)
	$(SILENT)cd $@/newlib && autoreconf -vfi $(SILENT_OUTPUT)
	$(SILENT)cp -rf $(TOOLCHAIN_NEWLIB_DIR)/port/* $@

newlib/configure: $(NEWLIB_BUILD_DIR)/config.status
$(NEWLIB_BUILD_DIR)/config.status: $(NEWLIB_DIR)
	$(call COMPILE,CONFIGURE,$@)
	$(SILENT)\
		cd $(dir $@) && \
		$(PWD)/$(NEWLIB_DIR)/configure \
			--target="$(TARGET)" \
			--prefix="$(PREFIX)" \
			--with-sysroot="$(TOOLCHAIN_SYSROOT)" \
			$(NEWLIB_CONFIGURE_FLAGS) \
		>  configure.log \
		2> configure.err

libc: newlib
newlib: newlib/configure
	$(call LOG,MAKE,newlib)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),all CFLAGS='$(ASFLAGS)',newlib/)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),install,newlib/)

.PHONY: libc newlib newlib/configure newlib/prepare newlib/tar

TO_CLEAN += $(NEWLIB_BUILD_DIR)
TO_DISTCLEAN += $(NEWLIB_DIR)
