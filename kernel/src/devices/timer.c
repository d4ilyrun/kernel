#include <kernel/devices/serial.h>
#include <kernel/devices/timer.h>
#include <kernel/logger.h>

#include <utils/macro.h>

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

static void timer_set_interval(u16 value)
{
    int counter = PIT_COUNTER(TIMER.counter);

    switch (TIMER.policy) {
    case TIMER_RO:
        break;
    case TIMER_RW_LSB:
        outb(counter, LSB(value));
        break;
    case TIMER_RW_MSB:
        outb(counter, MSB(value));
        break;
    case TIMER_RW:
        outb(counter, LSB(value));
        outb(counter, MSB(value));
        break;
    }
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
    timer_set_interval(TIMER_INTERNAL_FREQUENCY / frequency);
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
