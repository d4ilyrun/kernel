ASFLAGS    += -m32 -march=i686
NASMFLAGS  += -felf32

QEMU := qemu-system-i386

KERNEL_ARCH_SRCS := \
	crt0.S \
    gdt.S \
    gdt.c \
    interrupts.c \
    interrupts.asm \
    process.c \
    process.S \
    syscalls.c \
    mmu.c \
    setup.c \
    terminal.c \
    panic.c \
    devices/pic.c \
    devices/pit.c \
    devices/timer.c \
    devices/uart.c
