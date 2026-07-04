KERNEL_BUILD_DIR := $(BUILD_DIR)/$(KERNEL_DIR)
KERNEL_BIN       := $(KERNEL_BUILD_DIR)/kernel.bin

KERNEL_CPPFLAGS := -I$(KERNEL_BUILD_DIR)/$(INC_DIR)
KERNEL_CPPFLAGS += -DARCH=$(ARCH) -DKERNEL -DUACPI_FORMATTED_LOGGING
KERNEL_CFLAGS   :=
KERNEL_LDFLAGS  := -L$(KERNEL_BUILD_DIR)/$(LIB_DIR)
KERNEL_LDFLAGS  += -lk -lalgo -lpath -luacpi -lgcc

KERNEL_LDSCRIPT := $(BUILD_DIR)/kernel/linker.ld
KERNEL_LDFLAGS  += -T $(KERNEL_LDSCRIPT)

KERNEL_CFLAGS   += $(FREESTANDING_CFLAGS)
KERNEL_CPPFLAGS += $(FREESTANDING_CPPFLAGS)
KERNEL_LDFLAGS  += $(FREESTANDING_LDFLAGS)

ifneq ($(ARCH),)
  include $(KERNEL_DIR)/arch/$(ARCH)/build.mk
  ifeq ($(QEMU),)
    $(error Undefined environment variable: QEMU)
    $(error The arch specific 'build.mk' file must define the qemu executable to use for the target)
  endif
endif

KERNEL_SRCS := 	\
	main.c \
	fs/vfs.c \
	fs/file.c \
	fs/tar.c \
	fs/devtmpfs.c \
	fs/socket.c \
	fs/pipe.c \
	fs/shm.c \
	sys/sched.c \
	sys/syscalls.c \
	sys/process.c \
	sys/pci.c \
	sys/signal.c \
	sys/timer.c \
	sys/interrupts.c \
	misc/printk.c \
	misc/logger.c \
	misc/console.c \
	misc/symbols.c \
	misc/error.c \
	misc/exec.c \
	misc/elf32.c \
	misc/waitqueue.c \
	misc/worker.c \
	misc/semaphore.c \
	misc/uacpi.c \
	misc/user.c \
	memory/memory.c \
	memory/pmm.c \
	memory/vmm.c \
	memory/address_space.c \
	memory/vm_vnode.c \
	memory/slab.c \
	net/net.c \
	net/packet.c \
	net/socket.c \
	net/ethernet.c \
	net/ipv4.c \
	net/icmp.c \
	net/arp.c \
	net/interface.c \
	net/route.c \
	net/unix.c \
	memory/kmalloc.c \
	devices/device.c \
	devices/driver.c \
	devices/acpi.c \
	devices/pci.c \
	devices/block.c \
	devices/ethernet.c \
	devices/input.c \
	devices/ata.c \
	devices/rtl8139.c \
	devices/ramdisk.c \
	devices/framebuffer.c

KERNEL_OBJS += $(addsuffix .o, $(addprefix $(KERNEL_BUILD_DIR)/,$(KERNEL_SRCS)))
KERNEL_OBJS += $(addsuffix .o, $(addprefix $(KERNEL_BUILD_DIR)/arch/$(ARCH)/,$(KERNEL_ARCH_SRCS)))
DEPS += $(KERNEL_OBJS:.o=.d)

#
# Kernel build targets
#

KERNEL_CONFIG_HEADER := $(KERNEL_BUILD_DIR)/$(INC_DIR)/config.h
KERNEL_CPPFLAGS      += -include $(KERNEL_CONFIG_HEADER)

$(KERNEL_BIN): $(KERNEL_LDSCRIPT) $(KERNEL_OBJS)
	$(call COMPILE,LD,$@)
	$(SILENT)$(CC) $(KERNEL_OBJS) -o "$@" $(KERNEL_LDFLAGS) $(LDFLAGS)

$(KERNEL_BUILD_DIR)/%.c.o: $(KERNEL_DIR)/%.c $(KERNEL_CONFIG_HEADER) | libs
	$(call COMPILE,CC,$@)
	$(SILENT)$(CC) $(KERNEL_CPPFLAGS) $(CPPFLAGS) $(KERNEL_ASFLAGS) $(ASFLAGS) $(KERNEL_CFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(KERNEL_BUILD_DIR)/%.S.o: $(KERNEL_DIR)/%.S $(KERNEL_CONFIG_HEADER) | libs
	$(call COMPILE,AS,$@)
	$(SILENT)$(CC) $(KERNEL_CPPFLAGS) $(CPPFLAGS) $(KERNEL_ASFLAGS) $(ASFLAGS) $(KERNEL_CFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(KERNEL_BUILD_DIR)/%.asm.o: $(KERNEL_DIR)/%.asm
	$(call COMPILE,NASM,$@)
	$(SILENT)$(NASM) $(KERNEL_NASMFLAGS) $(NASMFLAGS) -o "$@" "$<"

$(KERNEL_BUILD_DIR)/%.ld: $(KERNEL_DIR)/%.ld $(KERNEL_CONFIG_HEADER)
	$(call COMPILE,CPP,$@)
	$(SILENT)$(CPP) $(CPPFLAGS) "$<" -o "$@"

.PHONY: kernel
kernel: $(KERNEL_BIN)

.PHONY: config
config: $(KERNEL_CONFIG_HEADER)
$(KERNEL_CONFIG_HEADER): $(REPO_ROOT)/.config
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

#
# Qemu targets
#

QEMU_TAP_IF ?= tap0
QEMU_HAS_TAP := $(shell test -d /sys/class/net/$(QEMU_TAP_IF) && echo y)

ifeq ($(QEMU_HAS_TAP),y)
# - RTL8139 NIC connected to a tap interface for easy dummping (also dumped into file)
QEMU_ARGS += -device rtl8139,netdev=net0 \
			 -netdev tap,id=net0,ifname=$(QEMU_TAP_IF),script=no,downscript=no \
			 -object filter-dump,id=f1,netdev=net0,file=$(BUILD_DIR)/network.dat
$(info Network TAP interface: $(QEMU_TAP_IF))
else
$(info Network TAP interface: $(QEMU_TAP_IF) (disabled))
endif

qemu: $(ISO)
	$(call LOG,QEMU,$^)
	$(call ASSERT_EXE_EXISTS,$(QEMU))
	$(SILENT)$(QEMU) -cdrom $(ISO) -serial stdio $(QEMU_ARGS)

qemu-server: $(ISO)
	$(call LOG,QEMU,$^)
	$(call ASSERT_EXE_EXISTS,$(QEMU))
	$(SILENT)$(QEMU) -cdrom $(ISO) -daemonize -s -S $(QEMU_ARGS)

TO_CLEAN += $(BUILD_DIR)/$(KERNEL_DIR)

.PHONY: qemu qemu-server
