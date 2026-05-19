#
# This file should be included to generate the necessary targets for
# ports of existing applications.
#

include $(REPO_ROOT)/functions.mk

$(call ASSERT_DEFINED,SRC_DIR)
$(call ASSERT_DEFINED,BUILD_DIR)
$(call ASSERT_DEFINED,INSTALL_DIR)
$(call ASSERT_DEFINED,PORT_NAME)
$(call ASSERT_DEFINED,PORT_TYPE)
$(call ASSERT_DEFINED,PORT_VERSION)
$(call ASSERT_DEFINED,PORT_URL)

PORT_ARCHIVE        ?= $(SRC_DIR)/$(notdir $(PORT_URL))
PORT_BUILD_DIR      ?= $(BUILD_DIR)/build/$(PORT_NAME)-$(PORT_VERSION)
PORT_STAMPS_DIR     ?= $(BUILD_DIR)/stamps/$(PORT_NAME)-$(PORT_VERSION)
PORT_PATCHES        ?= $(wildcard $(SRC_DIR)/patches/*.patch)

stamp-prepare  := $(PORT_STAMPS_DIR)/.stamp-prepare
stamp-generate := $(PORT_STAMPS_DIR)/.stamp-generate
stamp-config   := $(PORT_STAMPS_DIR)/.stamp-config
stamp-build    := $(PORT_STAMPS_DIR)/.stamp-build
stamp-install  := $(PORT_STAMPS_DIR)/.stamp-install

CFLAGS += -fdiagnostics-color=always

all: build

$(PORT_ARCHIVE):
	$(call LOG,WGET,$@)
	$(SILENT)wget $(PORT_URL) -O $(PORT_ARCHIVE)

$(stamp-prepare): $(PORT_ARCHIVE)
	$(SILENT)mkdir -p $(PORT_BUILD_DIR)
	$(SILENT)mkdir -p $(PORT_STAMPS_DIR)
	$(call LOG,EXTRACT,$(PORT_NAME))
	$(SILENT)tar xf $(PORT_ARCHIVE) -C $(dir $(PORT_BUILD_DIR))
	@for patch in $(PORT_PATCHES); do \
		$(call LOG_RAW,PATCH,$$(basename $${patch})); \
		patch -d $(PORT_BUILD_DIR) -p1 < $${patch} ${SILENT_OUTPUT}; \
	done
	$(SILENT)touch $(stamp-prepare) # generate stamp file

ifeq ($(PORT_TYPE), autogen)
$(stamp-generate): $(stamp-prepare)
	$(call LOG,GEN,$(PORT_NAME))
	$(SILENT)cd $(PORT_BUILD_DIR) && ./autogen.sh $(SILENT_OUTPUT)
	$(SILENT)touch $@ # generate stamp file
else
$(stamp-generate): $(stamp-prepare)
	$(SILENT)touch $@ # generate stamp file
endif

ifeq ($(PORT_TYPE),$(filter $(PORT_TYPE),automake autogen))

$(stamp-config): $(stamp-generate)
	$(call LOG,CONFIGURE,$(PORT_NAME))
	$(SILENT)\
		cd $(PORT_BUILD_DIR) && \
		CC="$(CC)" \
		CXX="$(CXX)" \
		CPP="$(CPP)" \
		LD="$(LD)" \
		AR="$(AR)" \
		CFLAGS="$(CFLAGS)" \
		./configure $(PORT_CONFIGURE_FLAGS) \
			--prefix="$(INSTALL_DIR)" \
			--host="$(HOST)" \
			--target="$(TARGET)" \
		>  configure.log \
		2> configure.err
	$(SILENT)touch $@ # generate stamp file

$(stamp-build): $(stamp-config)
	$(call MAKE_RECURSIVE,$(PORT_BUILD_DIR),all,$(PORT_NAME)/)
	$(SILENT)touch $@ # generate stamp file

$(stamp-install): $(stamp-build)
	$(call MAKE_RECURSIVE,$(PORT_BUILD_DIR),install,$(PORT_NAME)/)
	$(SILENT)touch $@ # generate stamp file

else
$(error unsupported PORT_TYPE: $(PORT_TYPE))
endif

prepare: $(stamp-prepare)
config:  $(stamp-config)
build:   $(stamp-build)
install: $(stamp-install)

clean:
	$(call LOG,CLEAN,$(PORT_NAME))
	$(SILENT)$(RM) -rf $(PORT_BUILD_DIR) $(PORT_STAMPS_DIR)

.PHONY: all prepare config build install clean
