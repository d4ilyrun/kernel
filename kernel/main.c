#define LOG_DOMAIN "main"

#include <kernel/console.h>
#include <kernel/cpu.h>
#include <kernel/device.h>
#include <kernel/devices/acpi.h>
#include <kernel/devices/block.h>
#include <kernel/devices/driver.h>
#include <kernel/devices/ramdisk.h>
#include <kernel/devices/uart.h>
#include <kernel/devices/framebuffer.h>
#include <kernel/elf32.h>
#include <kernel/execfmt.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/net/icmp.h>
#include <kernel/net/ipv4.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/semaphore.h>
#include <kernel/socket.h>
#include <kernel/symbols.h>
#include <kernel/syscalls.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>
#include <kernel/worker.h>

#include <utils/macro.h>
#include <utils/map.h>
#include <utils/math.h>

#include <multiboot.h>
#include <string.h>

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

void initcall_do_level(enum init_step level)
{

#define INITCALL_IMPORT_LEVEL(_level)           \
    extern void *_kernel_init_##_level##_start; \
    extern void *_kernel_init_##_level##_end;

#define INITCALL_SECTION(_level)                                    \
    {                                                               \
        .start = (struct initcall *)&_kernel_init_##_level##_start, \
        .end = (struct initcall *)&_kernel_init_##_level##_end,     \
    },

    /*
     * Import all initcall sections defined inside the linkerscript,
     * and generate a table of [start, end] address to easily iterate over
     * each section's initcalls.
     */
    MAP(INITCALL_IMPORT_LEVEL, INIT_STEPS);
    static const struct initcall_section initall_sections[] = {
        MAP(INITCALL_SECTION, INIT_STEPS)};

    const struct initcall_section *section = &initall_sections[level];
    error_t err;

    for (struct initcall *initcall = section->start; initcall < section->end;
         initcall += 1) {
        log(LOG_LEVEL_DEBUG, "initcall", "%s", initcall->name);
        err = initcall->call();
        if (err)
            log(LOG_LEVEL_WARN, "initcall", "%s failed with %pe",
                initcall->name, &err);
    }
}

static error_t kernel_mount_initfs(struct multiboot_tag_module *module)
{
    struct device *ramdisk;
    u32 mod_size;

    log_info("found initrd @ [" FMT32 ":" FMT32 "]", module->mod_start,
             module->mod_end);

    mod_size = module->mod_end - module->mod_start;
    ramdisk = ramdisk_create("initrd", module->mod_start, mod_size);
    if (IS_ERR(ramdisk))
        return ERR_FROM_PTR(ramdisk);

    return vfs_mount_root("tarfs", to_blkdev(ramdisk));
}

static error_t kernel_start_init_process(void)
{
    struct thread *init_thread;

    init_thread = process_execute_in_userland("/init");
    if (IS_ERR(init_thread))
        return ERR_FROM_PTR(init_thread);

    init_process = init_thread->process;
    init_thread->tid = 1;
    init_process->pid = 1;

    return E_SUCCESS;
}

void kernel_main(struct multiboot_info *mbt, unsigned int magic)
{
    error_t err;

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

    /*
     *
     */
    uart_init();
    console_set_active("uart");

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        PANIC("Invalid magic number recieved from multiboot "
              "bootloader: " FMT32,
              magic);
    }

    log(LOG_LEVEL_INFO, "kernel", "Starting");
    log(LOG_LEVEL_INFO, "kernel", "Size: %d bytes",
        KERNEL_CODE_END - KERNEL_CODE_START);

    FOREACH_MULTIBOOT_TAG (tag, &mbt_tmp) {
        if (tag->type == MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME) {
            struct multiboot_tag_string *btl = (void *)tag;
            log(LOG_LEVEL_INFO, "bootloader", "%s", btl->string);
        }
        if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE) {
            struct multiboot_tag_string *cmdline = (void *)tag;
            if (strnlen(cmdline->string, 0) != 0)
                log(LOG_LEVEL_INFO, "cmdline", "%s", cmdline->string);
        }
    }

    /*
     * Called with paging disabled to initialize the very first things
     * that need to be setup in the system.
     */
    initcall_do_level(INIT_STEP_BOOTSTRAP);

    /*
     * Now that we have a minimal working setup, we can enable paging
     * and initialize the virtual memory allocation API.
     *
     * After this step, we are able to allocate & free kernel memory.
     */
    memory_init(mbt);

    process_init_kernel_process();

    /*
     * Initialize arch-specific features:
     * - Interrupt controllers
     * - Timers
     * - ...
     */
    initcall_do_level(INIT_STEP_EARLY);

    /*
     * IRQs and controllers are setup, we can safely enable interrupts.
     */
    interrupts_enable();
    timer_start(TICKS_PER_SECOND);

    mbt_info = kmalloc(mbt_tmp.mbt.total_size, KMALLOC_KERNEL);
    memcpy(mbt_info, mbt_tmp.raw, mbt_tmp.mbt.total_size);

    /*
     * The initrd location is specfied by the bootloader through multiboot
     * modules. For convenience we assume that there is only one module,
     * which contains our initramfs.
     */
    FOREACH_MULTIBOOT_TAG (tag, mbt_info) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            err = kernel_mount_initfs((struct multiboot_tag_module *)tag);
            if (err)
                PANIC("Failed to mount initfs: %pe", &err);
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer_common *t = (void *)tag;
            struct framebuffer_params fb_params = {
                .width = t->framebuffer_width,
                .height = t->framebuffer_height,
                .pitch = t->framebuffer_pitch,
                .bpp = t->framebuffer_bpp,
            };

            framebuffer_register(t->framebuffer_addr, &fb_params);
            // console_set_active("fb0");
        }
    }

    acpi_init(mbt_info);
    driver_load_drivers();

    initcall_do_level(INIT_STEP_NORMAL);
    initcall_do_level(INIT_STEP_LATE);

    while (1);
    err = kernel_start_init_process();
    if (err)
        PANIC("failed to find a suitable init process: %pe", &err);

    thread_kill(current);
}
