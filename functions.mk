#
# Library of functions called by our build system.
#

ifeq ($(VERBOSE)$(V),)
SILENT := @
SILENT_OUTPUT := 1> /dev/null 2> /dev/null
endif

define LOG_RAW
	printf "[%s]\t%s\n" "$(1)" "$(2)"
endef

define LOG
	@$(LOG_RAW)
endef

# No other way to print a newline character ...
define newline


endef

# Easily assert that an executable exists
define ASSERT_EXE_EXISTS
	@$(foreach exe,$(1), \
		if ! which $(exe) 1>/dev/null 2>/dev/null ; then \
			echo "$(exe): not found in PATH" >&2; \
			exit 1; \
		fi; \
	)
endef

# Make sure that an executable uses a specific version
define CHECK_VERSION
  $(call ASSERT_EXE_EXISTS,$(1)) \
  if  ! $(1) --version | grep '$(2)' > /dev/null; then \
    echo "error: $(1): invalid version (expected $(2))" >&2; \
    exit 1; \
  fi
endef

ifneq ($(VERBOSE)$(V),)
define MAKE_RECURSIVE
  $(call LOG,MAKE,$(3)$(2))
  $(SILENT)$(MAKE) -C $(1) $(2) $(4)
endef
else
define MAKE_RECURSIVE
  $(call LOG,MAKE,$(3)$(2))
  $(SILENT)$(MAKE) -C $(1) $(2) $(4) 1> $(1)/make.log 2> $(1)/make.err
endef
endif

define ASSERT_DEFINED
$(if $($(1)),,$(error $(1) is not defined))
endef
