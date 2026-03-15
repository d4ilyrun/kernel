TOOLCHAIN_NEWLIB_DIR := $(TOOLCHAIN_DIR)/newlib

NEWLIB_VERSION			:= 4.5.0.20241231
NEWLIB_VERSION_SHORT	:= 4.5.0
NEWLIB_TAR_URL			:= ftp://sourceware.org/pub/newlib/newlib-$(NEWLIB_VERSION).tar.gz
NEWLIB_TAR				:= $(TOOLCHAIN_NEWLIB_DIR)/newlib-$(NEWLIB_VERSION).tar.gz
NEWLIB_BUILD_DIR		:= $(BUILD_DIR)/newlib/$(TARGET)-$(NEWLIB_VERSION_SHORT)

newlib/tar: $(NEWLIB_TAR)
$(NEWLIB_TAR):
	$(call COMPILE,WGET,$@)
	$(SILENT)$(WGET) $(NEWLIB_TAR_URL) -O $@

newlib/prepare: $(NEWLIB_BUILD_DIR)
$(NEWLIB_BUILD_DIR): $(NEWLIB_TAR)
	$(SILENT)$(call CHECK_VERSION,autoconf,2.69)
	$(SILENT)$(call CHECK_VERSION,automake,1.15.1)
	$(call COMPILE,EXTRACT,$@)
	$(SILENT)tar xf $(NEWLIB_TAR) -C $(BUILD_DIR)/newlib
	$(SILENT)mv $(BUILD_DIR)/newlib/newlib-$(NEWLIB_VERSION) $@
	$(call LOG,PATCH,$@)
	$(SILENT)cp -rf $(TOOLCHAIN_NEWLIB_DIR)/port/* $@
	$(call LOG,RECONF,$@)
	$(SILENT)cd $@/newlib && autoreconf -vfi $(SILENT_OUTPUT)
	$(SILENT)cp -rf $(TOOLCHAIN_NEWLIB_DIR)/port/* $@

newlib/install_headers: $(PREFIX)/include/newlib.h
$(PREFIX)/include/newlib.h: newlib/prepare
	$(call INSTALL, $(NEWLIB_BUILD_DIR)/newlib/libc/include/, $(PREFIX)/include)
	$(call INSTALL, $(NEWLIB_BUILD_DIR)/newlib/libc/sys/dailyrun/include/, $(PREFIX)/include)

newlib/configure: $(NEWLIB_BUILD_DIR)/config.status
$(NEWLIB_BUILD_DIR)/config.status: $(NEWLIB_BUILD_DIR) $(SYSROOT)
	$(call COMPILE,CONFIGURE,$@)
	$(SILENT)\
		cd $(dir $@) && \
		$(PWD)/$(NEWLIB_BUILD_DIR)/configure \
			--host="$(HOST)" \
			--target="$(TARGET)" \
			--prefix="$(PREFIX)" \
			--with-tooldir="$(PREFIX)" \
			--with-build-sysroot="$(SYSROOT)" \
			$(NEWLIB_CONFIGURE_FLAGS) \
		>  configure.log \
		2> configure.err

newlib: newlib/install_headers newlib/configure
	$(call LOG,MAKE,newlib)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),all,newlib/)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),install,newlib/)

.PHONY: newlib newlib/configure newlib/prepare newlib/tar newlib/install_headers

libc: newlib
libc/install_headers: newlib/install_headers

.PHONY: libc libc/install_headers

TO_CLEAN += $(NEWLIB_BUILD_DIR)
