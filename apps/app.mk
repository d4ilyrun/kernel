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

CFLAGS += -fdiagnostics-color=always
CFLAGS += -MMD -MP

all: build

build:   $(BUILD_DIR)/$(APP_EXECUTABLE)
install: $(INSTALL_DIR)/$(APP_EXECUTABLE)

prepare:
	mkdir -p $(dir $(BUILD_DIR)/$(APP_EXECUTABLE))

$(INSTALL_DIR)/$(APP_EXECUTABLE): prepare build
	install -D -m 755 $(BUILD_DIR)/$(APP_EXECUTABLE) $(INSTALL_DIR)/$(APP_EXECUTABLE)

$(BUILD_DIR)/$(APP_EXECUTABLE): prepare $(APP_OBJS)
	$(CC) $(APP_OBJS) -o $@ $(APP_LDFLAGS) $(LDFLAGS)

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c
	$(CC) $(APP_CPPFLAGS) $(CPPFLAGS) $(APP_CFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) -r $(BUILD_DIR)
	$(RM)    $(INSTALL_DIR)/bin/$(APP_NAME)

.PHONY: all prepare build install

-include $(APP_DEPS)
