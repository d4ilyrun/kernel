#
# This file should be included to generate the necessary targets for user applications.
#

define ASSERT_DEFINED
$(if $($(1)),,$(error $(1) is not defined))
endef

$(call ASSERT_DEFINED,SRC_DIR)
$(call ASSERT_DEFINED,BUILD_DIR)
$(call ASSERT_DEFINED,INSTALL_DIR)
$(call ASSERT_DEFINED,APP_NAME)
$(call ASSERT_DEFINED,APP_SOURCES)

APP_OBJS := $(addsuffix .o, $(addprefix $(BUILD_DIR)/,$(APP_SOURCES)))
APP_DEPS := $(APP_OBJS:.o=.d)

CFLAGS += -fdiagnostics-color=always
CFLAGS += -MMD -MP

all: build

build:   prepare $(BUILD_DIR)/$(APP_NAME)
install: $(INSTALL_DIR)/bin/$(APP_NAME)

prepare:
	mkdir -p $(BUILD_DIR) $(INSTALL_DIR)

$(INSTALL_DIR)/bin/$(APP_NAME): prepare build
	install -D -m 755 $(BUILD_DIR)/$(APP_NAME) $(INSTALL_DIR)/bin/$(APP_NAME)

$(BUILD_DIR)/$(APP_NAME): prepare $(APP_OBJS)
	$(CC) $(APP_OBJS) -o $(BUILD_DIR)/$(APP_NAME) $(APP_LDFLAGS) $(LDFLAGS)

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c
	$(CC) $(APP_CPPFLAGS) $(CPPFLAGS) $(APP_CFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) -r $(BUILD_DIR)
	$(RM)    $(INSTALL_DIR)/bin/$(APP_NAME)

.PHONY: all prepare build install

-include $(APP_DEPS)
