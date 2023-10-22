# Toolchain for x86
CC ?= i686-elf-gcc

CFLAGS 		= -std=c99 -Wall -Werror -Wextra -pedantic -ffreestanding
CPPFLAGS 	=
LDFLAGS 	= -nostdlib

ROOT 		= .
K_ROOT 		= $(ROOT)/kernel
LIB_ROOT 	= $(ROOT)/lib
SCRIPTS_ROOT= $(ROOT)/scripts

MAKE = make --no-print-directory

# Files to be remove by the target 'clean'
CLEAN_FILES = *.iso
# Directories to call 'make clean' inside
CLEAN_RECURSIVE =

# Opitmization flags
ifeq ($(DEBUG),y)
CFLAGS += -O0 -g3
QEMU_PARAMS += -s -S -daemonize
else
CFLAGS += -O2
QEMU_PARAMS += -serial stdio
endif

all: iso

include $(LIB_ROOT)/lib.mk
include $(K_ROOT)/kernel.mk

K_ISO = kernel.iso

iso: $(K_ISO)
$(K_ISO): $(K_TARGET)
	$(SCRIPTS_ROOT)/generate_iso.sh $< $@

qemu: $(K_ISO)
	@clear
	qemu-system-i386 -cdrom $^ $(QEMU_PARAMS)

ifeq ($(DEBUG),y)
gdb: .gdbinit qemu
	gdb -x .gdbinit $(K_SYM)
endif

clean:
	$(RM) $(CLEAN_FILES)
	@for dir in $(CLEAN_RECURSIVE); do \
		$(MAKE) -C $${dir} clean; \
	done

bear:
	bear -- make -B

.PHONY: clean iso kernel all qemu gdb bear
