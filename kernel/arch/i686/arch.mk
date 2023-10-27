K_ARCH_OBJS = terminal.o asm/gdt.o gdt.o interrupt.o setup.o
CPPFLAGS += -I$(K_ARCH_ROOT)/include
