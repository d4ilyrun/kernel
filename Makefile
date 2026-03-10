-include .config

CC   := $(CROSS_COMPILE)gcc
CPP  := $(CROSS_COMPILE)cpp
LD   := $(CROSS_COMPILE)ld
AR   := $(CROSS_COMPILE)ar
NASM := nasm

ifeq ($(ARCH),)
  $(info Target architecture: Undefined)
else
  $(info Target architecture: $(ARCH))
endif

ifneq ($(CROSS_COMPILE),)
  $(info Cross compiling for target: $(CROSS_COMPILE) ($(CC)))
else
  $(info Compiling using host toolchain ($(CC)))
endif

# If no -j option has been specified, use the maximum amount of cores available.
# It can also be overriden using the NCORES variable.
NCORES ?= $(patsubst -j%,%,$(filter -j%,$(MAKEFLAGS)))
ifeq ($(NCORES),)
NCORES := $(shell nproc)
endif
$(info Compiling sub-targets using $(NCORES) cores)

WGET := wget --no-verbose --show-progress
MAKE := $(MAKE) -j$(NCORES) --no-print-directory

ifneq ($(VERBOSE),y)
SILENT := @
SILENT_OUTPUT := 1> /dev/null 2> /dev/null
endif

BUILD_DIR		:= build
INC_DIR			:= include
LIB_DIR			:= lib
KERNEL_DIR		:= kernel
SCRIPTS_DIR		:= scripts
DOCS_DIR		:= docs
TOOLCHAIN_DIR	:= toolchain
ROOT_DIR		:= root
APPS_DIR		:= apps

DEBUG ?= y

CFLAGS   := -std=gnu11 -Werror -Wall -Wextra -MMD -MP
CPPFLAGS := -I$(INC_DIR)
LDFLAGS  := -L$(BUILD_DIR)/$(LIB_DIR)

CFLAGS += -fdiagnostics-color=always

FREESTANDING_CFLAGS    := -ffreestanding
FREESTANDING_CPPFLAGS  :=
FREESTANDING_LDFLAGS   := -nostdlib

CFLAGS   += $(FREESTANDING_CFLAGS)
CPPFLAGS += $(FREESTANDING_CPPFLAGS)
LDFLAGS  += $(FREESTANDING_LDFLAGS)

ifneq ($(DEBUG),)
CFLAGS   += -g3
CPPFLAGS += -DNDEBUG
endif

define LOG
	@printf "[%s]\t%s\n" "$(1)" "$(2)"
endef

define COMPILE
	$(call LOG,$(1),$(2))
	$(SILENT)mkdir -p $(dir $(2))
endef

define INSTALL
	$(call LOG,INSTALL,$(1) $(2))
	$(SILENT)rsync $(3) --mkpath -mrl $(1) $(2);
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

define REDIRECT_OUTPUT
  1> $(1).log 2> $(1).err
endef

ifeq ($(VERBOSE),y)
define MAKE_RECURSIVE
  $(call LOG,MAKE,$(3)$(2))
  $(SILENT)$(MAKE) -C $(1) $(2) $(4)
endef
else
define MAKE_RECURSIVE
  $(call LOG,MAKE,$(3)$(2))
  $(SILENT)$(MAKE) -C $(1) $(2) $(4) $(call REDIRECT_OUTPUT,$(1)/make)
endef
endif

define newline


endef

# Make sure that a package exists on the build system (i.e. an executable)
define CHECK_PACKAGE
  if ! which $(1) > /dev/null ; then \
    echo "error: $(1): package not found" >&2; \
    exit 1; \
  fi
endef

define CHECK_VERSION
  $(call CHECK_PACKAGE,$(1)); \
  if  ! $(1) --version | grep '$(2)' > /dev/null; then \
    echo "error: $(1): invalid version (expected $(2))" >&2; \
    exit 1; \
  fi
endef

all: kernel

include $(TOOLCHAIN_DIR)/build.mk
include $(LIB_DIR)/build.mk
include $(APPS_DIR)/build.mk
include $(KERNEL_DIR)/build.mk
include $(DOCS_DIR)/build.mk

GENERATED_CONFIG_HEADER := $(BUILD_DIR)/config.h

config: $(GENERATED_CONFIG_HEADER)
$(GENERATED_CONFIG_HEADER):
	$(call COMPILE,GEN,$@)
	@echo "/* Automatically generated, do not edit */" > $@
	@$(foreach v,$(filter CONFIG_%,$(.VARIABLES)), \
		if [ -n "$($(v))" ]; then \
			if [ "$($(v))" = "y" ]; then \
				echo "#define $(v) 1" >> $@; \
			else \
				echo "#define $(v) $($(v))" >> $@; \
			fi; \
		else \
			echo "/* $(v) is not set */" >> $@; \
		fi; \
	)

CPPFLAGS += -include $(GENERATED_CONFIG_HEADER)

.PHONY: config

$(BUILD_DIR)/%.c.o: %.c $(GENERATED_CONFIG_HEADER)
	$(call COMPILE,CC,$@)
	$(SILENT)$(CC) $(CPPFLAGS) $(ASFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/%.S.o: %.S $(GENERATED_CONFIG_HEADER)
	$(call COMPILE,AS,$@)
	$(SILENT)$(CC) $(CPPFLAGS) $(ASFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/%.asm.o: %.asm
	$(call COMPILE,NASM,$@)
	$(SILENT)$(NASM) $(NASMFLAGS) -o "$@" "$<"

$(BUILD_DIR)/%.ld: %.ld $(GENERATED_CONFIG_HEADER)
	$(call COMPILE,CPP,$@)
	$(SILENT)$(CPP) $(CPPFLAGS) "$<" -o "$@"

compile_commands.json:
	$(call COMPILE,GEN,$@)
	$(call ASSERT_EXE_EXISTS,bear)
	$(SILENT)bear -- $(MAKE) -B all

clangd:
	$(SILENT)echo -e > .clangd "\
	CompileFlags:\n\
	  Add:\n\
	    - --sysroot=$(SYSROOT)\n\
	    - --target=$(TARGET)\n\
	    - -isystem$(SYSROOT)/usr/lib/dailyrun/include\n\
	    - -isystem$(SYSROOT)/usr/lib/include\
"

#
# Build user directory
#
.PHONY: root
root:
	$(call INSTALL, $(ROOT_DIR)/, $(BUILD_DIR)/$(ROOT_DIR))

TO_CLEAN += $(BUILD_DIR)/$(ROOT_DIR)

#
# Remove build artifacts
#
clean/%:
	$(RM) -rf $(shell echo "$@" | sed "s/clean/$(BUILD_DIR)/")

.PHONY: clean
clean: apps/clean
	$(foreach to_clean,$(TO_CLEAN),$(RM) -rf $(to_clean) $(newline))

#
# Remove build artifacts and more
#
.PHONY: distclean
distclean: clean
	$(foreach to_clean,$(TO_DISTCLEAN),$(RM) -rf $(to_clean) $(newline))


-include $(DEPS)
