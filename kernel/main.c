#define LOG_DOMAIN "main"

#include <kernel/cpu.h>
#include <kernel/device.h>
#include <kernel/devices/acpi.h>
#include <kernel/devices/driver.h>
#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/elf32.h>
#include <kernel/execfmt.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/net/icmp.h>
#include <kernel/net/ipv4.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/semaphore.h>
#include <kernel/socket.h>
#include <kernel/symbols.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>
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

// Tasks used for manually testing
void kernel_test(void);
void kernel_task_timer(void *data);
void kernel_task_malloc(void *data);
void kernel_task_worker(void *data);
void kernel_task_mutex(void *data);
void kernel_task_ping(void *data);

void kernel_relocate_module(struct multiboot_tag_module *module)
{
    u32 mod_size;
    void *reloc;

    mod_size = module->mod_end - module->mod_start;
    mmu_identity_map(module->mod_start, module->mod_end,
                     PROT_READ | PROT_KERNEL);

    reloc = vm_alloc(&kernel_address_space, align_up(mod_size, PAGE_SIZE),
                     VM_READ | VM_WRITE);
    if (IS_ERR(reloc)) {
        log_err("failed to relocate module@" FMT32 ": E_NOMEM",
                module->mod_start);
        return;
    }

    memcpy(reloc, (void *)module->mod_start, mod_size);
    mmu_unmap_range(module->mod_start, module->mod_end);
    module->mod_end = (u32)reloc + mod_size - 1;
    module->mod_start = (u32)reloc;
}

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
            log(LOG_LEVEL_WARN, "initcall", "%s failed with %s", initcall->name,
                err_to_str(err));
    }
}

static error_t kernel_mount_initfs(void)
{
    struct multiboot_tag_module *initrd = NULL;
    vaddr_t start;
    vaddr_t end;

    /*
     * The initrd location is specfied by the bootloader through multiboot
     * modules. For convenience we assume that there is only one module,
     * which contains our initramfs.
     */
    FOREACH_MULTIBOOT_TAG (tag, mbt_info) {
        if (tag->type != MULTIBOOT_TAG_TYPE_MODULE)
            continue;
        initrd = (void *)tag;
        break;
    }

    if (initrd == NULL) {
        log_err("initrd module not found");
        return E_NOENT;
    }

    log_info("found initrd @ [" FMT32 ":" FMT32 "]", initrd->mod_start,
             initrd->mod_end);

    /*
     * TODO: Should be replaced with a ram device or something.
     */
    start = initrd->mod_start;
    end = initrd->mod_end + 1;

    return vfs_mount_root("tarfs", start, end);
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
     * Try to initialize the system's early console first. This lets us
     * print debug logs early on.
     */
    if (uart_init() != E_SUCCESS) {
        // TODO: arch_reboot();
    }

    tty_init();

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        PANIC("Invalid magic number recieved from multiboot "
              "bootloader: " FMT32,
              magic);
    }

    log(LOG_LEVEL_INFO, "kernel", "Starting");
    log(LOG_LEVEL_INFO, "kernel", "Size: %d bytes",
        KERNEL_CODE_END - KERNEL_CODE_START);

    /*
     * Called with paging disabled to initialize the very first things
     * that need to be setup in the system.
     */
    initcall_do_level(INIT_STEP_BOOTSTRAP);

    /*
     * Now that we have a minimal working setup, we can enable paging
     * and initialize the virtual memory allocation API.
     * After this step, we are able to allocate & free kernel memory as usual.
     */
    if (!pmm_init(mbt))
        PANIC("Could not initialize the physical memory manager");

    log_info("Initializing MMU");
    if (!mmu_init())
        PANIC("Failed to initialize virtual address space");

    address_space_init(&kernel_address_space);
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
    timer_start(HZ);

    mbt_info = kmalloc(mbt_tmp.mbt.total_size, KMALLOC_KERNEL);
    memcpy(mbt_info, mbt_tmp.raw, mbt_tmp.mbt.total_size);

    /*
     * We need to relocate the content of the multiboot modules inside the
     * kernel address space if we want them to be accessible from every
     * processes
     */
    FOREACH_MULTIBOOT_TAG (tag, mbt_info) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            kernel_relocate_module((struct multiboot_tag_module *)tag);
        }
    }

    acpi_init(mbt_info);
    driver_load_drivers();

    err = kernel_mount_initfs();
    if (err)
        PANIC("Failed to mount initfs: %s", err_to_str(err));

    initcall_do_level(INIT_STEP_NORMAL);
    initcall_do_level(INIT_STEP_LATE);

    /*
     * Testing !
     */
    kernel_test();

    err = kernel_start_init_process();
    if (err)
        PANIC("failed to find a suitable init process: %s", err_to_str(err));

    thread_kill(current);
}

void kernel_test(void)
{
    sched_new_thread_create(kernel_task_malloc, NULL, THREAD_KERNEL);
    sched_new_thread_create(kernel_task_worker, NULL, THREAD_KERNEL);
    sched_new_thread_create(kernel_task_mutex, NULL, THREAD_KERNEL);
    sched_new_thread_create(kernel_task_ping, NULL, THREAD_KERNEL);

    thread_t *kernel_timer_test = thread_spawn(
        current->process, kernel_task_timer, NULL, NULL, THREAD_KERNEL);
    sched_new_thread(kernel_timer_test);

    log_dbg("Re-started task: '%s' (TID=%d)", current->process->name,
            current->tid);

    while (1) {
        timer_wait_ms(1000);
        log_info("Elapsed miliseconds: %lld", timer_get_ms());
        if (timer_get_ms() > 2000) {
            thread_kill(kernel_timer_test);
            break;
        }
    }
}

// TASKS USED FOR MANUALLY TESTING FEATURES

#undef LOG_DOMAIN
#define LOG_DOMAIN "kmalloc"
void kernel_task_malloc(void *data)
{
    uint32_t *invalid_free = kcalloc(4, sizeof(uint32_t), KMALLOC_DEFAULT);
    uint32_t *kmalloc_addresses = kcalloc(4, sizeof(uint32_t), KMALLOC_DEFAULT);

    UNUSED(kmalloc_addresses);
    UNUSED(data);

    for (int i = 0; i < 4; ++i)
        kfree(invalid_free); // test anti corruption free magic

    for (int i = 0; i < 4; ++i)
        kmalloc_addresses[i] = (uint32_t)&kmalloc_addresses[i];

    log_array(kmalloc_addresses, 4);

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

#undef LOG_DOMAIN
#define LOG_DOMAIN "ktask"

void kernel_task_timer(void *data)
{
    UNUSED(data);

    log_dbg("Started task: '%s' (TID=%d)", current->process->name,
            current->tid);

    while (1) {
        timer_wait_ms(1000);
        log_info("Elapsed miliseconds: %lld", timer_get_ms());
    }
}

#undef LOG_DOMAIN
#define LOG_DOMAIN "kworker"

static void __kernel_task_worker(void *data)
{
    const char *message = data;
    log_info("message: %s", message);
    timer_wait_ms(1000);
}

void kernel_task_worker(void *data)
{
    const char *message = "Hello from the worker";
    DECLARE_WORKER(worker);
    error_t ret;

    UNUSED(data);

    ret = worker_init(&worker);
    if (ret) {
        log_info("worker_init() failed: %s", err_to_str(ret));
        return;
    }

    log_info("%lld: creating worker", timer_get_ms());
    worker_start(&worker, __kernel_task_worker, (void *)message);
    worker_wait(&worker);
    log_info("%lld: worker finished", timer_get_ms());

    log_info("%lld: restarting worker", timer_get_ms());
    worker_start(&worker, __kernel_task_worker, (void *)message);
    worker_wait(&worker);
    log_info("%lld: worker finished", timer_get_ms());

    log_info("%lld: retesting worker", timer_get_ms());
    worker_wait(&worker);
    log_info("%lld: worker finished", timer_get_ms());
}

#undef LOG_DOMAIN
#define LOG_DOMAIN "kmutex"

static DECLARE_MUTEX(mutex);

static void __kernel_task_mutex(void *data)
{
    semaphore_t *mutex = data;

    log_info("taking mutex");
    semaphore_acquire(mutex);
    log_info("mutex acquired");
    for (int i = 0; i < 5; ++i)
        timer_wait_ms(1000);
    log_info("releasing mutex");
    semaphore_release(mutex);
    log_info("mutex released");
}

void kernel_task_mutex(void *data)
{
    struct thread *thread_a;
    struct thread *thread_b;

    UNUSED(data);

    thread_a = thread_spawn(&kernel_process, __kernel_task_mutex, &mutex, NULL,
                            THREAD_KERNEL);
    thread_b = thread_spawn(&kernel_process, __kernel_task_mutex, &mutex, NULL,
                            THREAD_KERNEL);

    sched_new_thread(thread_a);
    sched_new_thread(thread_b);
}

#undef LOG_DOMAIN
#define LOG_DOMAIN "kping"

/***/
struct icmp_echo_header {
    struct icmp_header icmp;
    __be u16 identifier;
    __be u16 sequence;
};

void kernel_task_ping(void *data)
{
    struct socket *socket;
    struct sockaddr_in addr;
    struct file *fd;
    struct icmp_echo_header ping_request;
    struct icmp_echo_header ping_reply;
    u16 sequence = 0;
    error_t err;

    UNUSED(data);

    addr.sin_family = AF_INET;

    socket = socket_alloc();
    if (IS_ERR(socket)) {
        log_err("failed to create socket");
        return;
    }

    err = socket_init(socket, AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (err) {
        log_err("failed to init socket: %s", err_to_str(err));
        return;
    }

    fd = socket->file;

    addr.sin_addr = IPV4(10, 1, 1, 2);
    err = file_bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (err) {
        log_err("failed to bind: %s", err_to_str(err));
        return;
    }

    addr.sin_addr = IPV4(10, 1, 1, 1);
    do {
        err = file_connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err) {
            log_warn("failed to connect: %s", err_to_str(err));
            timer_wait_ms(1000);
        }
    } while (err);

    ping_request.icmp.type = ICMP_ECHO_REQUEST;

    while (1) {

        ping_request.sequence = hton(sequence++);
        log_info("request: seq=%d", ntoh(ping_request.sequence));

        err = file_send(fd, (void *)&ping_request, sizeof(ping_request), 0);
        if (err) {
            log_err("send: %s", err_to_str(err));
            continue;
        }

        timer_wait_ms(1000);

        err = file_recv(fd, (void *)&ping_reply, sizeof(ping_reply), 0);
        if (err) {
            log_err("recv: %s", err_to_str(err));
            continue;
        }

        log_info("reply: seq=%d [%s]", ntoh(ping_reply.sequence),
                 (ping_reply.sequence == ping_request.sequence) ? "OK" : "KO");
    }
}
