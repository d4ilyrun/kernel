KERNEL_BIN := $(BUILD_DIR)/$(KERNEL_DIR)/kernel.bin
KERNEL_ISO := $(BUILD_DIR)/$(KERNEL_DIR)/kernel.iso

CPPFLAGS += -DARCH=$(ARCH)

ifneq ($(ARCH),)
  KERNEL_ARCH_DIR := $(KERNEL_DIR)/arch/$(ARCH)
  include $(KERNEL_ARCH_DIR)/build.mk
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
	fs/socket.c \
	sys/device.c \
	sys/sched.c \
	sys/syscalls.c \
	sys/process.c \
	sys/pci.c \
	misc/printk.c \
	misc/logger.c \
	misc/console.c \
	misc/symbols.c \
	misc/error.c \
	misc/execfmt.c \
	misc/elf32.c \
	misc/waitqueue.c \
	misc/worker.c \
	misc/semaphore.c \
	memory/pmm.c \
	memory/vmm.c \
	net/net.c \
	net/packet.c \
	net/socket.c \
	net/ethernet.c \
	net/ipv4.c \
	net/arp.c \
	net/interface.c \
	net/route.c \
	memory/kmalloc.c \
	devices/driver.c \
	devices/uacpi.c \
	devices/acpi.c \
	devices/pci.c \
	devices/ethernet.c \
	devices/rtl8139.c

KERNEL_OBJS += $(addsuffix .o, $(addprefix $(BUILD_DIR)/$(KERNEL_DIR)/,$(KERNEL_SRCS)))
KERNEL_OBJS += $(addsuffix .o, $(addprefix $(BUILD_DIR)/$(KERNEL_ARCH_DIR)/,$(KERNEL_ARCH_SRCS)))
DEPS += $(KERNEL_OBJS:.o=.d)

KERNEL_LDSCRIPT := $(BUILD_DIR)/kernel/linker.ld

KERNEL_LIBS := k algo path uacpi

$(KERNEL_BIN): CPPFLAGS += -DKERNEL -DUACPI_FORMATTED_LOGGING
$(KERNEL_BIN): | $(KERNEL_LDSCRIPT)
$(KERNEL_BIN): | $(foreach lib,$(KERNEL_LIBS),lib$(lib))
$(KERNEL_BIN): $(KERNEL_OBJS)
	$(call COMPILE,LD,$@)
	$(SILENT)$(CC) $^ -o "$@" -T $(KERNEL_LDSCRIPT) $(foreach lib,$(KERNEL_LIBS),-l$(lib)) $(LDFLAGS) -lgcc

$(KERNEL_ISO): $(KERNEL_BIN)
	$(call COMPILE,ISO,$@)
	$(call ASSERT_EXE_EXISTS,grub-mkrescue mformat)
	$(SILENT)$(SCRIPTS_DIR)/generate_iso.sh $< $@

.PHONY: kernel iso
kernel: $(KERNEL_BIN)
iso: $(KERNEL_ISO)

QEMU_TAP_IF ?= tap0
QEMU_HAS_TAP := $(shell ip link show $(QEMU_TAP_IF) &> "/dev/null"; echo "$$?")

ifeq ($(QEMU_HAS_TAP),0)
# - RTL8139 NIC connected to a tap interface for easy dummping (also dumped into file)
QEMU_ARGS += -device rtl8139,netdev=net0 \
			 -netdev tap,id=net0,ifname=$(QEMU_TAP_IF),script=no,downscript=no \
			 -object filter-dump,id=f1,netdev=net0,file=$(BUILD_DIR)/network.dat
$(info Network TAP interface: $(QEMU_TAP_IF))
else
$(info Network TAP interface: $(QEMU_TAP_IF) (disabled))
endif

qemu: $(KERNEL_ISO)
	$(call LOG,QEMU,$^)
	$(call ASSERT_EXE_EXISTS,$(QEMU))
	$(SILENT)$(QEMU) -cdrom $(KERNEL_ISO) -nographic $(QEMU_ARGS)

qemu-server: $(KERNEL_ISO)
	$(call LOG,QEMU,$^)
	$(call ASSERT_EXE_EXISTS,$(QEMU))
	$(SILENT)$(QEMU) -cdrom $(KERNEL_ISO) -daemonize -s -S $(QEMU_ARGS)

TO_CLEAN += $(BUILD_DIR)/$(KERNEL_DIR) $(KERNEL_BIN) $(KERNEL_ISO) $(BUILD_DIR)/kernel.map $(BUILD_DIR)/kernel.sym

.PHONY: qemu qemu-server
