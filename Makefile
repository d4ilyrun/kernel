-include .config

CC   := $(CROSS_COMPILE)$(CC)
CXX  := $(CROSS_COMPILE)$(CXX)
CPP  := $(CROSS_COMPILE)$(CPP)
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

BUILD_DIR		:= build
INC_DIR			:= include
LIB_DIR			:= lib
KERNEL_DIR		:= kernel
SCRIPTS_DIR		:= scripts
DOCS_DIR		:= docs
TOOLCHAIN_DIR	:= toolchain
ROOT_DIR		:= root
APPS_DIR		:= apps

BUILD_ROOT_DIR ?= $(PWD)/$(BUILD_DIR)/$(ROOT_DIR)
TO_CLEAN += $(BUILD_DIR)/$(ROOT_DIR)

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

include $(PWD)/functions.mk

define COMPILE
	$(call LOG,$(1),$(2))
	$(SILENT)mkdir -p $(dir $(2))
endef

define INSTALL
	$(call LOG,INSTALL,$(1) $(2))
	$(SILENT)rsync $(3) --mkpath -mrl $(1) $(2);
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
	    - --sysroot=$(PREFIX)\n\
	    - --target=$(TARGET)\n\
	    - -isystem$(PREFIX)/usr/include\
"

#
# Remove build artifacts
#
clean/%:
	$(call LOG,CLEAN,$(@:clean/%=%))
	$(SILENT)$(RM) -rf $(shell echo "$@" | sed "s/clean/$(BUILD_DIR)/")

.PHONY: clean
clean: apps/clean
	$(call LOG,CLEAN,all)
	$(foreach to_clean,$(TO_CLEAN),$(SILENT)$(RM) -rf $(to_clean) $(newline))

#
# Remove build artifacts and more
#
.PHONY: distclean
distclean: clean
	$(call LOG,DISTCLEAN,all)
	$(foreach to_clean,$(TO_DISTCLEAN),$(SILENT)$(RM) -rf $(to_clean) $(newline))


-include $(DEPS)
