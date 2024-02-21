#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <kernel/i686/cpu_ops.h>
#include <kernel/i686/devices/pit.h>

#include <utils/macro.h>

/// PIT's control register IO port
#define PIT_CONTROL_REGISTER (0x43)

/// Counters registers' I/O ports: 0x40, 0x41, 0x42
#define PIT_COUNTER(_counter) (0x40 + (_counter))

typedef enum {
    PIT_RO = 0,     //< Read Only
    PIT_RW_LSB = 1, //< R/W LSB only
    PIT_RW_MSB = 2, //< R/W MSB only
    PIT_RW = 3,     //< R/W LSB first, then MSB
} pit_rw_policy;

/** The PIT's control register (8bits). */
typedef struct {
    u8 bcd : 1;
    u8 mode : 3;
    pit_rw_policy policy : 2;
    u8 counter : 2; //< Counter to setup (0-2)
} pit_config;

static pit_config pit_channels[PIT_CHANNELS_COUNT] = {
    {.counter = 0, .policy = PIT_RW, .bcd = 0},
    {.counter = 1, .policy = PIT_RW, .bcd = 0},
    {.counter = 2, .policy = PIT_RW, .bcd = 0},
};

static u32 pit_channel_frequencies[PIT_CHANNELS_COUNT] = {0};

static void pit_set_divider(pit_channel channel, u32 value)
{
    int counter = PIT_COUNTER(channel);

    // If the desired frequency is too low (say 1Hz), the resulting divider may
    // be higher than 65536 (1.9M for 1Hz). But the value we write to the
    // divider register is a 16bits one.
    //
    // We explicitely limit it to UINT16_MAX to avoid weird behaviours because
    // of this overflow (a frequency of 1Hz being faster than 1MHz for example).

    if (value > UINT16_MAX) {
        log_warn("PIT", "Divider value does not fit into 16 bits: " LOG_FMT_32,
                 value);
        log_warn("PIT", "Using divider of UINT16_MAX (18.2Hz)");
        value = UINT16_MAX;
    }

    switch (pit_channels[channel].policy) {
    case PIT_RO:
        break;
    case PIT_RW_LSB:
        outb(counter, LSB(value));
        pit_channel_frequencies[channel] = PIT_INTERNAL_FREQUENCY / LSB(value);
        break;
    case PIT_RW_MSB:
        outb(counter, MSB(value));
        pit_channel_frequencies[channel] =
            PIT_INTERNAL_FREQUENCY / (MSB(value) << 8);
        break;
    case PIT_RW:
        outb(counter, LSB(value));
        outb(counter, MSB(value));
        pit_channel_frequencies[channel] =
            PIT_INTERNAL_FREQUENCY / (value & 0xFFFF);
        break;
    }

    // Round up frequency if needed
    if (PIT_INTERNAL_FREQUENCY % value > value / 2)
        pit_channel_frequencies[channel] += 1;

    log_dbg("PIT", "New frequency divisor value for channel %d: %d (%d Hz)",
            channel, value, pit_channel_frequencies[channel]);
}

u32 pit_config_channel(pit_channel channel, u32 frequency, pit_mode mode)
{
    if (frequency == 0) {
        log_err("PIT",
                "Trying to configure channel %d using NULL frequency. Skip.",
                channel);
        return pit_channel_frequencies[channel];
    }

    if (frequency > PIT_MAX_CHANNEL_FREQUENCY) {
        log_warn("PIT",
                 "Timer's frequency is higher than the maximum possible value "
                 "frequency (" LOG_FMT_32 " > " LOG_FMT_32 ")",
                 frequency, PIT_MAX_CHANNEL_FREQUENCY);

        frequency = PIT_MAX_CHANNEL_FREQUENCY;
        log_warn("PIT",
                 "Limiting frequency to the maximum value (" LOG_FMT_32 ")",
                 frequency);
    }

    if (channel >= PIT_CHANNELS_COUNT) {
        log_err("PIT", "Invalid channel: " LOG_FMT_8, channel);
        return -1;
    }

    // Convert 8b control register to raw 8b value
    pit_channels[channel].mode = mode;
    u8 *raw_config = (u8 *)&pit_channels[channel];

    outb(PIT_CONTROL_REGISTER, *raw_config);
    pit_set_divider(channel, PIT_INTERNAL_FREQUENCY / frequency);

    return pit_channel_frequencies[channel];
}

u16 pit_read_channel(pit_channel channel)
{
    int counter = PIT_COUNTER(channel);
    u16 value;

    switch (pit_channels[channel].policy) {
    case PIT_RW_LSB:
        return inb(counter);
    case PIT_RW_MSB:
        return inb(counter) << 8;

    case PIT_RO:
    case PIT_RW:
        // When reading 2 bytes at once, we need to use the LATCH command to
        // avoid the timer being updated during 2 read operations (inb).
        outb(PIT_CONTROL_REGISTER, pit_channels[channel].counter << 6);
        value = inb(PIT_CONTROL_REGISTER);
        value |= inb(PIT_CONTROL_REGISTER) << 8;
        return value;
    }
}
