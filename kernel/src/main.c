#include <kernel/devices/pic.h>
#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>

#include <multiboot.h>
#include <utils/macro.h>

void arch_setup(void);

void kernel_main(struct multiboot_info *mbt, unsigned int magic)
{
    // TODO: panic if bootlodaer magic is invalid
    UNUSED(magic);

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
    pic_reset();
    // IRQs are setup, we can safely enable interrupts
    interrupts_enable();

    timer_start(TIMER_TICK_FREQUENCY);

    pmm_init(mbt);

    ASM("int $0");

    while (1) {
        timer_wait_ms(1000);
        log_info("MAIN", "Elapsed miliseconds: %d", gettime());
    }
}
