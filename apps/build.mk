APPS := hello

define DEFINE_USER_APP_TARGET
.PHONY: apps/$(1)/$(2)
apps/$(1)/$(2):
	$$(call MAKE_RECURSIVE, $$(APPS_DIR)/$(1),$(2),apps/$(1)/, \
		CC="$(CC)" \
		SRC_DIR="$(PWD)/$(APPS_DIR)/$(1)" \
		BUILD_DIR="$(PWD)/$(BUILD_DIR)/$(APPS_DIR)/$(1)" \
		INSTALL_DIR="$(BUILD_ROOT_DIR)" \
	)
endef

define ADD_USER_APP
.PHONY: apps/$(1)
apps/$(1): apps/$(1)/build apps/$(1)/install

$(call DEFINE_USER_APP_TARGET,$(1),build)
$(call DEFINE_USER_APP_TARGET,$(1),install)
$(call DEFINE_USER_APP_TARGET,$(1),clean)
endef

$(foreach app, $(APPS), $(eval $(call ADD_USER_APP,$(app))))

apps: apps/build apps/install
apps/build:    $(foreach app, $(APPS), apps/$(app)/build)
apps/install:  $(foreach app, $(APPS), apps/$(app)/install)
apps/clean:    $(foreach app, $(APPS), apps/$(app)/clean)
	$(call LOG,CLEAN,apps)
	$(SILENT)$(RM) -r $(PWD)/$(BUILD_DIR)/$(APPS_DIR)

.PHONY: apps apps/install apps/clean apps/build
