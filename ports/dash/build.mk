PORTS_DASH_DIR := $(PORTS_DIR)/dash

DASH_VERSION		:= 0.5.12
DASH_TAR_URL		:= https://web.git.kernel.org/pub/scm/utils/dash/dash.git/snapshot/dash-$(DASH_VERSION).tar.gz
DASH_DIR			:= $(PORTS_DASH_DIR)/dash-$(DASH_VERSION)
DASH_TAR			:= $(DASH_DIR).tar.gz
DASH_BUILD_DIR	    := $(BUILD_DIR)/ports/dash-$(DASH_VERSION)
DASH_EXE            := $(DASH_BUILD_DIR)/src/dash

dash/tar: $(DASH_TAR)
$(eval $(call GENERATE_TAR_FETCH_RULE, $(DASH_TAR_URL), $(DASH_TAR)))

dash/prepare: $(DASH_DIR)
$(DASH_DIR):  $(DASH_TAR)
	$(call COMPILE,EXTRACT,$@)
	$(SILENT)tar xf  $< -C $(dir $@)
	$(call LOG,AUTOGEN,$@)
	$(SILENT)cd $@ && ./autogen.sh $(SILENT_OUTPUT)

dash/configure: $(DASH_BUILD_DIR)/config.status
$(eval $(call GENERATE_CONFIGURE_RULE,$(DASH_DIR),$(DASH_BUILD_DIR)))

dash/build: $(DASH_EXE)
$(DASH_EXE): $(DASH_BUILD_DIR)/config.status
	$(call LOG,MAKE,dash)
	$(call MAKE_RECURSIVE,$(DASH_BUILD_DIR),all,dash/)

dash: dash/build

.PHONY: dash dash/build dash/configure dash/prepare dash/tar

TO_CLEAN += $(DASH_DIR) $(DASH_BUILD_DIR)
