# To be included by the main Makefile at the root of the project.
#
# Defines the variables and targets related to building the kernel.

K_TARGET = kernel.bin
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
		 )

# Add architecture specific object files to kernel object files
K_OBJS += $(addprefix $(K_ARCH_ROOT)/,$(K_ARCH_OBJS))

CLEAN_FILES += $(K_OBJS) $(K_TARGET)

$(K_TARGET): libc
$(K_TARGET): LDFLAGS += -L$(LIBC_ROOT)
$(K_TARGET): CPPFLAGS += -I$(K_ROOT)/include -I$(LIBC_ROOT)/include
$(K_TARGET): CPPFLAGS += $(addprefix -D,$(K_CONFIG))
$(K_TARGET): $(K_OBJS)
	$(CC) -T $(K_ARCH_ROOT)/linker.ld -o $@ $(CPPFLAGS) $(CFLAGS) $(K_OBJS) $(LDFLAGS) -lgcc -lc
