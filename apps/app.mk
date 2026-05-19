#
# This file should be included to generate the necessary targets for user applications.
#

include $(REPO_ROOT)/functions.mk

$(call ASSERT_DEFINED,SRC_DIR)
$(call ASSERT_DEFINED,BUILD_DIR)
$(call ASSERT_DEFINED,INSTALL_DIR)
$(call ASSERT_DEFINED,APP_NAME)
$(call ASSERT_DEFINED,APP_EXECUTABLE)
$(call ASSERT_DEFINED,APP_SOURCES)

APP_OBJS := $(addsuffix .o, $(addprefix $(BUILD_DIR)/,$(APP_SOURCES)))
APP_DEPS := $(APP_OBJS:.o=.d)

stamp-prepare  := $(BUILD_DIR)/.stamp-prepare
stamp-build    := $(BUILD_DIR)/.stamp-build
stamp-install  := $(BUILD_DIR)/.stamp-install
stamp-clean    := $(BUILD_DIR)/.stamp-clean

CFLAGS += -fdiagnostics-color=always
CFLAGS += -MMD -MP

CPPFLAGS += -I$(INSTALL_DIR)/usr/include
LDFLAGS  += -L$(INSTALL_DIR)/lib -L$(INSTALL_DIR)/usr/lib

all: build

prepare: $(stamp-prepare)
build: $(stamp-build)
install: $(stamp-install)
clean: $(stamp-clean)

$(stamp-prepare):
	$(SILENT)mkdir -p $(dir $(BUILD_DIR)/$(APP_EXECUTABLE))
	$(SILENT)touch $(stamp-prepare) # generate stamp file

$(stamp-build): $(BUILD_DIR)/$(APP_EXECUTABLE)
	$(SILENT)mkdir -p $(dir $(BUILD_DIR)/$(APP_EXECUTABLE))
	$(SILENT)touch $(stamp-build) # generate stamp file

$(stamp-install): $(stamp-build)
	$(SILENT)install -D -m 755 $(BUILD_DIR)/$(APP_EXECUTABLE) $(INSTALL_DIR)/$(APP_EXECUTABLE)
	$(SILENT)touch $(stamp-install) # generate stamp file

$(stamp-clean):
	$(SILENT)$(RM) -r $(BUILD_DIR)
	$(SILENT)$(RM)    $(INSTALL_DIR)/bin/$(APP_NAME)

$(BUILD_DIR)/$(APP_EXECUTABLE): $(stamp-prepare) $(APP_OBJS)
	$(call LOG,LD,$(notdir $@))
	$(SILENT)$(CC) $(APP_OBJS) -o $@ $(APP_LDFLAGS) $(LDFLAGS)

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c
	$(call LOG,CC,$(notdir $@))
	$(SILENT)$(CC) $(APP_CPPFLAGS) $(CPPFLAGS) $(APP_CFLAGS) $(CFLAGS) -c $< -o $@

.PHONY: all prepare build install

-include $(APP_DEPS)
