#include <kernel/devices/serial.h>
#include <kernel/devices/timer.h>
#include <kernel/logger.h>

#include <utils/macro.h>
#include <utils/types.h>

#include "kernel/devices/pic.h"

// FIXME: Include the ARCH/interrupts.h automatically inside kernel/interrupts.h
//        This defeats the idea of separting kernel from architercture specific
#include <kernel/i686/interrupts.h>

/**
 * This is where we keep track of the number of intervals reported by the timer.
 *
 * This MUST be incremented EACH time we recieve an interrupt of type IRQ_TIMER.
 */
static volatile u64 timer_ticks_counter = 0;

/**
 * The current frequency of the timer.
 * This value is updated each time we call \c timer_set_divider.
 */
static volatile u32 timer_kernel_frequency = 0;

/// PIT's control register IO port
#define PIT_CONTROL_REGISTER (0x43)

/// Counters registers' I/O ports: 0x40, 0x41, 0x42
#define PIT_COUNTER(_counter) (0x40 + (_counter))

typedef enum {
    TIMER_RO = 0,     //< Read Only
    TIMER_RW_LSB = 1, //< R/W LSB only
    TIMER_RW_MSB = 2, //< R/W MSB only
    TIMER_RW = 3,     //< R/W LSB first, then MSB
} timer_rw_policy;

/** The PIT's control register (8bits). */
typedef struct {
    u8 bcd : 1;
    u8 mode : 3;
    timer_rw_policy policy : 2;
    u8 counter : 2; //< Counter to setup (0-2)
} timer_config;

static const timer_config TIMER = {
    .bcd = 0,
    .mode = 2, // Trigger an interrupt each time we complete an interval
    .policy = TIMER_RW,
    .counter = 0,
};

static void timer_set_divider(u32 value)
{
    int counter = PIT_COUNTER(TIMER.counter);

    // If the desired frequency is too low (say 1Hz), the resulting divider may
    // be higher than 65536 (1.9M for 1Hz). But the value we write to the
    // divider register is a 16bits one.
    //
    // We explicitely limit it to UINT16_MAX to avoid weird behaviours because
    // of this overflow (a frequency of 1Hz being faster than 1MHz for example).

    if (value > UINT16_MAX) {
        log_warn("TIMER",
                 "Divider value does not fit into 16 bits: " LOG_FMT_32, value);
        log_warn("TIMER", "Using divider of UINT16_MAX (18.2Hz)");
        value = UINT16_MAX;
    }

    switch (TIMER.policy) {
    case TIMER_RO:
        break;
    case TIMER_RW_LSB:
        outb(counter, LSB(value));
        timer_kernel_frequency = TIMER_INTERNAL_FREQUENCY / LSB(value);
        break;
    case TIMER_RW_MSB:
        outb(counter, MSB(value));
        timer_kernel_frequency = TIMER_INTERNAL_FREQUENCY / (MSB(value) << 8);
        break;
    case TIMER_RW:
        outb(counter, LSB(value));
        outb(counter, MSB(value));
        timer_kernel_frequency = TIMER_INTERNAL_FREQUENCY / (value & 0xFFFF);
        break;
    }

    // Round up frequency if needed
    if (TIMER_INTERNAL_FREQUENCY % value > value / 2)
        timer_kernel_frequency += 1;

    log_dbg("TIMER", "New frequency divisor value: %d (%d Hz)", value,
            timer_kernel_frequency);
}

void timer_start(u32 frequency)
{
    if (frequency == 0) {
        log_err("TIMER", "Trying to start a timer using NULL frequency");
        return;
    }

    if (frequency > TIMER_INTERNAL_FREQUENCY) {
        log_warn("TIMER",
                 "Timer's frequency is higher than the maximum internal "
                 "frequency (" LOG_FMT_32 " > " LOG_FMT_32 ")",
                 frequency, TIMER_INTERNAL_FREQUENCY);

        frequency = TIMER_INTERNAL_FREQUENCY;
        log_warn("TIMER",
                 "Limiting the frequency to the maximum value (" LOG_FMT_32 ")",
                 TIMER_INTERNAL_FREQUENCY);
    }

    if (TIMER.counter > 2) {
        log_err("TIMER", "Invalid config: invalid counter (" LOG_FMT_8 ")",
                TIMER.counter);
        return;
    }

    // Convert 8b control register to raw 8b value
    u8 *raw_config = (u8 *)&TIMER;

    outb(PIT_CONTROL_REGISTER, *raw_config);
    timer_set_divider(TIMER_INTERNAL_FREQUENCY / frequency);

    // Setup the timer's IRQ handler
    // It is responsible for updating our internal timer representation
    interrupts_set_handler(PIC_MASTER_VECTOR + IRQ_TIMER,
                           INTERRUPT_HANDLER(irq_timer));
    pic_enable_irq(IRQ_TIMER);
}

u16 timer_read(void)
{
    int counter = PIT_COUNTER(TIMER.counter);
    u16 value;

    switch (TIMER.policy) {
    case TIMER_RW_LSB:
        return inb(counter);
    case TIMER_RW_MSB:
        return inb(counter) << 8;

    case TIMER_RO:
    case TIMER_RW:
        // When reading 2 bytes at once, we need to use the LATCH command to
        // avoid the timer being updated during 2 read operations (inb).
        outb(PIT_CONTROL_REGISTER, TIMER.counter << 6);
        value = inb(PIT_CONTROL_REGISTER);
        value |= inb(PIT_CONTROL_REGISTER) << 8;
        return value;
    }
}

DEFINE_INTERRUPT_HANDLER(irq_timer)
{
    UNUSED(frame);

    if (timer_ticks_counter == UINT64_MAX) {
        log_warn("TIMER", "The internal timer has reached its max capacity.");
        log_warn("TIMER", "THIS WILL CAUSE AN OVERFLOW!");
    }

    timer_ticks_counter += 1;

    pic_eoi(IRQ_TIMER);
}

u64 timer_gettick(void)
{
    return timer_ticks_counter;
}

void timer_wait_ms(u64 ms)
{
    const u64 start = timer_ticks_counter;
    const u64 end = start + (1000 * timer_kernel_frequency) / ms;

    WAIT_FOR(timer_ticks_counter >= end);
}
