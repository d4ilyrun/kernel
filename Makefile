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
  $(info Cross compiling for target: $(CROSS_COMPILE))
else
  $(info Compiling using host toolchain)
endif

NCORES ?= $(shell nproc)
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

DEBUG ?= y

CFLAGS   := -std=gnu11 -Werror -Wall -Wextra -MMD -MP
CPPFLAGS := -I$(INC_DIR)
LDFLAGS  := -L$(BUILD_DIR)

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
  $(SILENT)$(MAKE) -C $(1) $(2)
endef
else
define MAKE_RECURSIVE
  $(call LOG,MAKE,$(3)$(2))
  $(SILENT)$(MAKE) -C $(1) $(2) $(call REDIRECT_OUTPUT,$(1)/make)
endef
endif

define newline


endef

all: kernel

include $(TOOLCHAIN_DIR)/build.mk
include $(LIB_DIR)/build.mk
include $(KERNEL_DIR)/build.mk
include $(DOCS_DIR)/build.mk

$(BUILD_DIR)/%.c.o: %.c
	$(call COMPILE,CC,$@)
	$(SILENT)$(CC) $(CPPFLAGS) $(ASFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/%.S.o: %.S
	$(call COMPILE,AS,$@)
	$(SILENT)$(CC) $(CPPFLAGS) $(ASFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(BUILD_DIR)/%.asm.o: %.asm
	$(call COMPILE,NASM,$@)
	$(SILENT)$(NASM) $(CPPFLAGS) $(NASMFLAGS) -o "$@" "$<"

$(BUILD_DIR)/%.ld: %.ld
	$(call COMPILE,CPP,$@)
	$(SILENT)$(CPP) $(CPPFLAGS) "$<" -o "$@"

compile_commands.json:
	$(call COMPILE,GEN,$@)
	$(call ASSERT_EXE_EXISTS,bear)
	$(SILENT)bear -- $(MAKE) -B all

clean/%:
	$(RM) -rf $(shell echo "$@" | sed "s/clean/$(BUILD_DIR)/")

clean:
	$(foreach to_clean,$(TO_CLEAN),$(RM) -rf $(to_clean) $(newline))

distclean: clean
	$(foreach to_clean,$(TO_DISTCLEAN),$(RM) -rf $(to_clean) $(newline))

.PHONY: clean distclean

-include $(DEPS)
