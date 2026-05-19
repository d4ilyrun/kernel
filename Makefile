-include .config

__TOP_LEVEL__ := 1
REPO_ROOT     := $(PWD)

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
MAKE := $(MAKE) -j$(NCORES)
endif
$(info Compiling sub-targets using $(NCORES) cores)

WGET := wget --no-verbose --show-progress
MAKE := $(MAKE) --no-print-directory --silent

BUILD_DIR		:= build
INC_DIR			:= include
LIB_DIR			:= lib
KERNEL_DIR		:= kernel
SCRIPTS_DIR		:= scripts
DOCS_DIR		:= docs
TOOLCHAIN_DIR	:= toolchain
ROOT_DIR		:= root
APPS_DIR		:= apps

ISO       := $(BUILD_DIR)/dailyrun.iso
INITRAMFS := $(BUILD_DIR)/initramfs.tar
TO_CLEAN  += $(ISO) $(INITRAMFS)

BUILD_ROOT_DIR ?= $(PWD)/$(BUILD_DIR)/$(ROOT_DIR)
TO_CLEAN += $(BUILD_DIR)/$(ROOT_DIR)

DEBUG ?= y

CFLAGS   := -std=gnu11 -Werror -Wall -Wextra -MMD -MP
CFLAGS   += -fdiagnostics-color=always
CPPFLAGS += -I$(INC_DIR)

FREESTANDING_CFLAGS    := -ffreestanding
FREESTANDING_CPPFLAGS  :=
FREESTANDING_LDFLAGS   := -nostdlib

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
	$(call LOG,INST,$(1) $(2))
	$(SILENT)rsync $(3) --mkpath -mrl $(1) $(2);
endef

all: kernel

include $(TOOLCHAIN_DIR)/build.mk
include $(KERNEL_DIR)/build.mk
include $(LIB_DIR)/build.mk
include $(APPS_DIR)/build.mk
include $(DOCS_DIR)/build.mk

compile_commands.json:
	$(call COMPILE,GEN,$@)
	$(call ASSERT_EXE_EXISTS,bear)
	$(SILENT)bear -- $(MAKE) -B all

$(INITRAMFS): apps/install
	$(call INSTALL,$(ROOT_DIR)/,$(BUILD_ROOT_DIR))
	$(call COMPILE,INITRAMFS,$@)
	$(SILENT)cd $(BUILD_ROOT_DIR) && tar -cf $(REPO_ROOT)/$@ *

# overridable via config file
export CONFIG_GRAPHICS_WIDTH  ?= 1280
export CONFIG_GRAPHICS_HEIGHT ?= 720
export CONFIG_GRAPHICS_DEPTH  ?= 32

$(ISO): $(KERNEL_BIN) $(INITRAMFS)
	$(call COMPILE,ISO,$@)
	$(call ASSERT_EXE_EXISTS,grub-mkrescue mformat)
	$(SILENT)$(SCRIPTS_DIR)/generate_iso.sh $@ $(KERNEL_BIN) $(INITRAMFS)

TO_CLEAN += $(ISO) $(BUILD_DIR)/kernel.map $(BUILD_DIR)/kernel.sym
TO_CLEAN += $(INITRAMFS)

.PHONY: initramfs iso
initramfs: $(INITRAMFS)
iso: $(ISO)

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
