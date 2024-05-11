#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
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
    if (!mmu_init() || !mmu_start_paging())
        PANIC("Failed to initialize virtual address space");

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

    vmm_init(KERNEL_CODE_END, align_down(ADDRESS_SPACE_END, PAGE_SIZE));

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

    while (1) {
        timer_wait_ms(1000);
        log_info("MAIN", "Elapsed miliseconds: %d", gettime());
    }
}
