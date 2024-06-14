#include <kernel/cpu.h>
#include <kernel/device.h>
#include <kernel/devices/acpi.h>
#include <kernel/devices/driver.h>
#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/symbols.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <multiboot.h>
#include <string.h>

void arch_setup(void);

static struct multiboot_info *mbt_info;

// Temporarily store the content of the multiboot info structure before
// relocating it later (into mbt_info), since the original structure is
// not acccessible anymore once we activated paging, and that is size is
// dynamic (depends on the tags used). We hardcode it to a PAGE here since
// this should be plenty enough.
//
// We could release this once the kernel is started (similar to linux's __init).
union {
    u8 raw[PAGE_SIZE];
    struct multiboot_info mbt;
} mbt_tmp;

void kernel_task_timer(void *data)
{
    UNUSED(data);

    log_dbg("TASK", "Started task: '%s'", current_process->name);

    while (1) {
        timer_wait_ms(1000);
        log_info("TASK", "Elapsed miliseconds: %d", gettime());
    }
}

void kernel_main(struct multiboot_info *mbt, unsigned int magic)
{
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        PANIC("Invalid magic number recieved from multiboot "
              "bootloader: " LOG_FMT_32,
              magic);
    }

    memcpy(mbt_tmp.raw, mbt, mbt->total_size);

    // FIXME: Find how to clear pending keyboard IRQs inherited from bootloader
    //
    // At this stage we still might have pending IRQs waiting to be processed.
    // These come from untreated keyboard inputs during the bootloader phase.
    //
    // They get treated as a Segment Overrun Exception (0x9) once interrupts
    // become enabled eventually, since this is the default vector for Keyboard
    // IRQs.
    //
    // This seems to make the kernel hang for some reason (or at least prevent
    // any further keyboard interactions).

    interrupts_disable();

    uart_reset();
    tty_init();

    arch_setup();

    // IRQs are setup, we can safely enable interrupts
    interrupts_enable();

    timer_start(TIMER_TICK_FREQUENCY);

    if (!pmm_init(mbt))
        PANIC("Could not initialize the physical memory manager");

    log_info("START", "Initializing MMU");
    if (!mmu_init())
        PANIC("Failed to initialize virtual address space");

    log_info("START", "Initializing kernel VMM");
    vmm_init(&kernel_vmm, KERNEL_MEMORY_START, KERNEL_MEMORY_END);
    kernel_startup_process.vmm = &kernel_vmm;

    mbt_info = kmalloc(mbt_tmp.mbt.total_size, KMALLOC_KERNEL);
    memcpy(mbt_info, mbt_tmp.raw, mbt_tmp.mbt.total_size);

    // We need to identity map the content of the multiboot modules since they
    // are marked as available inside the memory_map passed on by Grub.
    FOREACH_MULTIBOOT_TAG (tag, mbt_info) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *module = (void *)tag;
            mmu_identity_map(module->mod_start, module->mod_end, PROT_READ);
        }
    }

    scheduler_init();

    driver_load_drivers();
    acpi_init(mbt_info);
    acpi_start_devices();

    ASM("int $0");

    u32 page = pmm_allocate(PMM_MAP_KERNEL);
    log_variable(page);
    mmu_map(0xFFFF1000, page, PROT_NONE);
    *(volatile u8 *)0xFFFF1235 = 0x42; // No page fault
    log_variable(*(volatile u8 *)0xFFFF1235);
    mmu_map(0x12341000, page, PROT_NONE);
    *(volatile u8 *)0x12341235 = 0x69; // No page fault, Same page
    log_variable(*(volatile u8 *)0xFFFF1235);
    mmu_unmap(0xFFFF1000);

    const kernel_symbol_t *symbol =
        kernel_symbol_from_address((u32)printf + 32);
    log_info("MAIN", "PRINTF ? (%s, " LOG_FMT_32 ")",
             kernel_symbol_name(symbol), symbol->address);

    {
        u32 *a = mmap(0, PAGE_SIZE, 0, 0);
        u32 *b = mmap(0, PAGE_SIZE * 2, 0, 0);
        u32 *c = mmap(0, PAGE_SIZE, 0, 0);
        u32 *e = mmap((void *)0xd0000000, PAGE_SIZE, 0, 0);

        u32 *addresses =
            mmap((void *)0xa0000000, PAGE_SIZE * 5, PROT_READ | PROT_WRITE, 0);

        for (int i = 0; i < 4; ++i)
            addresses[i] = (u32)&addresses[i];

        log_array("MAIN", addresses, 4);

        munmap(a, PAGE_SIZE);
        munmap(b, PAGE_SIZE * 2);
        munmap(c, PAGE_SIZE);
        munmap(e, PAGE_SIZE);
        munmap(addresses, PAGE_SIZE * 5);
    }

    {
        uint32_t *invalid_free = kcalloc(4, sizeof(uint32_t), KMALLOC_DEFAULT);
        uint32_t *kmalloc_addresses =
            kcalloc(4, sizeof(uint32_t), KMALLOC_DEFAULT);

        UNUSED(kmalloc_addresses);

        for (int i = 0; i < 4; ++i)
            kfree(invalid_free); // test anti corruption free magic

        for (int i = 0; i < 4; ++i)
            kmalloc_addresses[i] = (uint32_t)&kmalloc_addresses[i];

        log_array("MAIN", kmalloc_addresses, 4);

        kfree(kmalloc_addresses);

        kfree(kmalloc(4 * PAGE_SIZE, KMALLOC_DEFAULT));

        uint8_t *tata = kmalloc(KERNEL_STACK_SIZE, KMALLOC_KERNEL);
        tata[KERNEL_STACK_SIZE - 100] = 1;
        kfree(tata);

        uint32_t **blocks = kcalloc(8, sizeof(uint32_t *), KMALLOC_DEFAULT);
        for (int i = 0; i < 8; ++i) {
            blocks[i] = kmalloc(64 * sizeof(uint32_t), KMALLOC_DEFAULT);
            for (int j = 0; j < 64; ++j)
                blocks[i][j] = i * j;
        }

        for (int i = 0; i < 8; ++i)
            kfree(blocks[i]);
        kfree(blocks);
    }

    {
        struct multiboot_tag_module *ramdev_module = NULL;

        // Temporary: for convenience we assume there is only one module, which
        // contains our initramfs
        FOREACH_MULTIBOOT_TAG (tag, mbt_info) {
            if (tag->type != MULTIBOOT_TAG_TYPE_MODULE)
                continue;
            ramdev_module = (void *)tag;
            break;
        }

        if (ramdev_module == NULL)
            log_err("mbt", "No module found");

        log_dbg("mbt", "ramdev@" LOG_FMT_32, ramdev_module);
        log_dbg("mbt", "ramdev[" LOG_FMT_32 ":" LOG_FMT_32 "]",
                ramdev_module->mod_start, ramdev_module->mod_end);

        dev_t *ramdev =
            device_new(ramdev_module->mod_start,
                       ramdev_module->mod_end - ramdev_module->mod_start + 1);

        error_t ret = vfs_mount_root("tarfs", ramdev);
        log_dbg("init", "mount_root: %s", err_to_str(ret));
        log_info("init", "Searching for '/bin/busybox'");
        vnode_t *busybox = vfs_find_by_path("/bin/busybox");
        if (IS_ERR(busybox))
            log_err("init", "Could not find busybox: %s",
                    err_to_str(ERR_FROM_PTR(busybox)));

        ret = vfs_mount("/bin", "tarfs", ramdev);
        if (ret) {
            log_err("rootfs", "Failed to mount into rootfs: %s",
                    err_to_str(ret));
        } else {
            busybox = vfs_find_by_path("/bin/usr/bin");
            if (IS_ERR(busybox))
                log_err("rootfs",
                        "Could not find requested path inside mounted fs: %s",
                        err_to_str(ERR_FROM_PTR(busybox)));
            busybox = vfs_find_by_path("/bin/busybox");
            if (!IS_ERR(busybox)) {
                log_err("rootfs", "Should not be able to find old busybox");
                vfs_vnode_release(busybox);
            }
            if ((ret = vfs_unmount("/bin")))
                log_err("rootfs", "Failed to unmount '/bin': %s",
                        err_to_str(ret));
            if ((ret = vfs_unmount("/bin") != E_INVAL))
                log_err("rootfs", "Should not be able to unmount twice");
            log_dbg("rootfs", "creating file: %s",
                    err_to_str(vfs_create_at("/usr/bin/gcc///", VNODE_FILE)));
        }
    }

    process_t *kernel_timer_test =
        process_create("ktimer_test", kernel_task_timer, NULL);
    sched_new_process(kernel_timer_test);

    log_dbg("TASK", "Re-started task: '%s'", current_process->name);

    while (1) {
        timer_wait_ms(1000);
        log_info("MAIN", "Elapsed miliseconds: %d", gettime());
        if (BETWEEN(gettime(), 5000, 6000))
            process_kill(kernel_timer_test);
    }
}
