#
# This file should be included to generate libraries.
#

include $(REPO_ROOT)/functions.mk

# This file should be included from the top-level makefile
$(call ASSERT_DEFINED,__TOP_LEVEL__)

$(call ASSERT_DEFINED,LIB_NAME)
$(call ASSERT_DEFINED,LIB_SRC_DIR)
$(call ASSERT_DEFINED,LIB_INCLUDE_DIR)
$(call ASSERT_DEFINED,LIB_SOURCES)

LIB_BUILD_DIR := $(BUILD_DIR)/$(LIB_DIR)/$(LIB_NAME)

ifeq ($(LIB_TARGET),kernel)
LIB_INSTALL_DIR := $(BUILD_DIR)/$(KERNEL_DIR)/$(LIB_DIR)
INCLUDE_INSTALL_DIR := $(BUILD_DIR)/$(KERNEL_DIR)/$(INC_DIR)
else ifeq ($(LIB_TARGET),user)
LIB_INSTALL_DIR := $(BUILD_DIR)/$(ROOT_DIR)/usr/lib
INCLUDE_INSTALL_DIR := $(BUILD_DIR)/$(ROOT_DIR)/usr/include
else
$(error No target or invalid target specified for the $(LIB_NAME) library)
endif

LIB_OBJS := $(addsuffix .o, $(addprefix $(LIB_BUILD_DIR)/,$(LIB_SOURCES)))
LIB_DEPS := $(LIB_OBJS:.o=.d)

define LIB_TARGETS

$(LIB_BUILD_DIR)/.stamp-prepare:
	$(SILENT)mkdir -p $(LIB_BUILD_DIR)
	$(SILENT)touch $$@ # generate stamp file

$(LIB_BUILD_DIR)/.stamp-build: $(LIB_BUILD_DIR)/$(LIB_NAME).a
	$(SILENT)touch $$@ # generate stamp file

$(LIB_BUILD_DIR)/.stamp-install: $(LIB_BUILD_DIR)/.stamp-build
	$$(call INSTALL,$(LIB_BUILD_DIR)/$(LIB_NAME).a, $(LIB_INSTALL_DIR)/)
	$$(call INSTALL,$(LIB_INCLUDE_DIR)/, $(INCLUDE_INSTALL_DIR))
	$(SILENT)touch $$@ # generate stamp file

$(LIB_BUILD_DIR)/.stamp-clean:
	$(SILENT)$(RM) -r $(LIB_BUILD_DIR)
	$(SILENT)$(RM)    $(INSTALL_DIR)/$(LIB_NAME).a

$(LIB_BUILD_DIR)/$(LIB_NAME).a: $(LIB_BUILD_DIR)/.stamp-prepare $(LIB_OBJS)
	$$(call LOG,AR,$$(notdir $$@))
	$(SILENT)$$(AR) -rcs $$@ $(LIB_OBJS)

$(LIB_BUILD_DIR)/%.c.o: $(LIB_SRC_DIR)/%.c
	$$(call LOG,CC,$$(notdir $$@))
	$(SILENT)mkdir -p $$(dir $$@)
	$(SILENT)$$(CC) $(LIB_CPPFLAGS) -I$(LIB_INCLUDE_DIR) $$(CPPFLAGS) $(LIB_CFLAGS) $$(CFLAGS) -c $$< -o $$@

endef

$(eval $(LIB_TARGETS))

DEPS += $(LIB_DEPS)
