define GENERATE_TAR_FETCH_RULE
$(2):
	$$(call COMPILE,WGET,$$@)
	$$(SILENT)$$(WGET) $(1) -O $$@
endef

define GENERATE_CONFIGURE_RULE
$(2)/config.status: $(1)
	$$(call COMPILE,CONFIGURE,$$@)
	$$(SILENT)\
		cd $$(dir $$@) && \
		$$(PWD)/$(1)/configure \
			--target="$$(TARGET)" \
			--prefix="$$(PREFIX)" \
			$(3) \
		>  configure.log \
		2> configure.err
endef

define GENERATE_MAKE_RULE
$(2):
	$(call LOG,MAKE,newlib)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),all CFLAGS='$(ASFLAGS)',newlib/)
	$(call MAKE_RECURSIVE,$(NEWLIB_BUILD_DIR),install,newlib/)
endef

include $(PORTS_DIR)/dash/build.mk
