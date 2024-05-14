#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/symbols.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>
#include <kernel/vmm.h>

#include <utils/macro.h>
#include <utils/math.h>

#include <multiboot.h>

void arch_setup(void);

void kernel_main(struct multiboot_info *mbt, unsigned int magic)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        PANIC("Invalid magic number recieved from multiboot "
              "bootloader: " LOG_FMT_32,
              magic);
    }

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

    while (1) {
        timer_wait_ms(1000);
        log_info("MAIN", "Elapsed miliseconds: %d", gettime());
    }
}
