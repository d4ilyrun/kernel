# To be included by the main Makefile at the root of the project.
#
# Defines the variables and targets related to building the kernel.

K_ELF = kernel.elf
K_BIN = kernel.bin
K_SYM = kernel.sym

K_TARGET = $(K_BIN)
kernel: $(K_TARGET)

# Kernel configuration
include $(K_ROOT)/config.mk

# Include acrchitecture specific makefile
K_ARCH_ROOT = $(K_ROOT)/arch/$(K_ARCH)
include $(K_ARCH_ROOT)/arch.mk

# Kernel object files
K_OBJS = $(addprefix $(K_ROOT)/src/, \
		 crt0.o \
		 main.o \
		 logger.o \
		 syscalls.o \
		 devices/pic.o \
		 devices/uart.o \
		 )

# Add architecture specific object files to kernel object files
K_OBJS += $(addprefix $(K_ARCH_ROOT)/,$(K_ARCH_OBJS))

CLEAN_FILES += $(K_OBJS) $(K_ELF) $(K_BIN) $(K_SYM)

$(K_TARGET): libc
$(K_TARGET): LDFLAGS += -L$(LIBC_ROOT)
$(K_TARGET): CPPFLAGS += -I$(K_ROOT)/include -I$(LIBC_ROOT)/include
$(K_TARGET): CPPFLAGS += $(addprefix -D,$(K_CONFIG))
$(K_TARGET): $(K_OBJS)
ifeq ($(DEBUG),y)
	$(CC) -T $(K_ARCH_ROOT)/linker.ld -o $(K_BIN) $(CPPFLAGS) $(CFLAGS) $(K_OBJS) $(LDFLAGS) -lgcc -lc
	objcopy --only-keep-debug $(K_BIN) $(K_SYM)
	objcopy --strip-unneeded $(K_BIN)
else
	$(CC) -T $(K_ARCH_ROOT)/linker.ld -o $(K_BIN) $(CPPFLAGS) $(CFLAGS) $(K_OBJS) $(LDFLAGS) -lgcc -lc
endif
