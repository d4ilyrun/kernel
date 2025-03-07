define DEFINE_STATIC_LIBRARY_AT

$(1)_DIR  := $$(LIB_DIR)/$(2)
$(1)_OBJS := $$(addsuffix \
	.o, \
	$$(addprefix \
		$$(BUILD_DIR)/$$($(1)_DIR)/$(4)/, \
		$(5) \
	) \
)

CPPFLAGS += -I$$($(1)_DIR)/$(3)

.PHONY: $(1)
$(1): $(BUILD_DIR)/$(1).a

$(BUILD_DIR)/$(1).a: CFLAGS += -fPIC $$($(1)_CFLAGS)
$(BUILD_DIR)/$(1).a: $$($(1)_OBJS)
	$$(call COMPILE,AR,$$@)
	$$(SILENT)$$(AR) -rcs $$@ $$^

DEPS += $$($(1)_OBJS:.o=.d)

TO_CLEAN += $$(BUILD_DIR)/$(1).a

endef

define DEFINE_STATIC_LIBRARY
$(call DEFINE_STATIC_LIBRARY_AT,$(1),$(1),include,src,$(2))
endef

TO_CLEAN += $(BUILD_DIR)/$(LIB_DIR)

include $(LIB_DIR)/libtest/build.mk
include $(LIB_DIR)/libk/build.mk
include $(LIB_DIR)/libalgo/build.mk
include $(LIB_DIR)/libpath/build.mk
include $(LIB_DIR)/uacpi/build.mk

.PHONY: check tests
tests: $(TESTS)
check: tests
