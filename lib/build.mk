LIBS := $(foreach dir,$(shell find $(LIB_DIR) -mindepth 1 -maxdepth 1 -type d),$(notdir $(dir)))

include $(foreach lib, $(LIBS), $(LIB_DIR)/$(lib)/build.mk)

.PHONY: libs
libs:   $(foreach lib, $(LIBS), $(BUILD_DIR)/$(LIB_DIR)/$(lib)/.stamp-install)

TO_CLEAN += $(BUILD_DIR)/$(LIB_DIR)
