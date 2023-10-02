# Toolchain for x86
CC ?= i686-elf-gcc

CFLAGS 		= -std=c99 -Wall -Werror -Wextra -pedantic -ffreestanding
CPPFLAGS 	=
LDFLAGS 	= -nostdlib

ROOT 		= .
K_ROOT 		= $(ROOT)/kernel
LIB_ROOT 	= $(ROOT)/lib

MAKE = make --no-print-directory

# Files to be remove by the target 'clean'
CLEAN_FILES =
# Directories to call 'make clean' inside
CLEAN_RECURSIVE =

# Opitmization flags
CFLAGS += -O2

all: iso

include $(LIB_ROOT)/lib.mk
include $(K_ROOT)/kernel.mk

iso: $(K_TARGET) # TODO: build an actual iso file

clean:
	$(RM) $(CLEAN_TARGETS)
	@for dir in $(CLEAN_RECURSIVE); do \
		$(MAKE) -C $${dir} clean; \
	done

bear:
	bear -- make -B

.PHONY: clean iso kernel all
