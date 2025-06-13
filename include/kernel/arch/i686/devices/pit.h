/**
 * @file kernel/arch/i686/devices/pit.h
 *
 * @defgroup timer_x86 Timer - x86
 * @ingroup timer
 * @ingroup x86
 *
 * # i8254 Programmable Interval Timer (PIT)
 *
 * Any interaction done with the PIT should be done through
 * the functions defined inside this header.
 *
 * ## Design
 *
 * The i8254 PIT has an internal frequency of 1.19 MHz, and 3 separate
 * counters. Each counter must be configured with one of 7 modes, and a
 * frequency. To be more accurate we can only specify the divider, which applied
 * to the internal PIT frequency (1.9MHz), computes the actual frequency fo the
 * timer. Each time the counter reaches the computed limit, it triggers an
 * @ref IRQ_TIMER
 *
 * @see
 *   * https://wiki.osdev.org/Programmable_Interval_Timer
 *   * https://k.lse.epita.fr/data/8254.pdf
 *   * https://k.lse.epita.fr/internals/8254_controller.html
 *
 * @{
 */

#ifndef KERNEL_ARCH_I686_DEVICES_PIT_H
#define KERNEL_ARCH_I686_DEVICES_PIT_H

#include <kernel/types.h>
#include <kernel/error.h>

#define PIT_INTERNAL_FREQUENCY (1193182)

#define PIT_MIN_CHANNEL_FREQUENCY (19)
#define PIT_MAX_CHANNEL_FREQUENCY (PIT_INTERNAL_FREQUENCY)

#define PIT_CHANNELS_COUNT (3)

/**
 * @enum pit_channel
 * @brief The different PIT channels available
 */
typedef enum pit_channel {
    PIT_CHANNEL_TIMER,   //< CPU Timer
    PIT_CHANNEL_DRAM,    //< Legacy. Used to refresh DRAM
    PIT_CHANNEL_SPEAKER, //< Connected to the PC speaker
} pit_channel;

/**
 * @enum pit_mode
 * @brief The different programmable modes for a channel
 */
typedef enum {
    PIT_TRIGGER_LOW = 0,
    PIT_TRIGGER_HIGH,
    PIT_RATE_GENERATOR,
    PIT_PWM,
    PIT_SW_STROBE,
    PIT_HW_STROBE,
} pit_mode;

/**
 * @brief Configure a single PIT channel.
 *
 * We can only specify the channel's frequency, and the interrupt
 * trigger condition.
 * The read/write policy is forcefully set to 16bits.
 *
 * @note The frequency should be between 19 and PIT_INTERNAL_FREQUENCY
 *
 * @warning Because all frequencies are not necessarily available (maths and
 * boundaries), the resulting frequency might be slightly adjusted.
 *
 * @return The new channel frequency (after adjustments), -1 if invalid
 * channel
 */
error_t pit_config_channel(pit_channel, u32 frequency, pit_mode);

/** Read the current value inside the channel's counter */
u16 pit_read_channel(pit_channel);

#endif /*  KERNEL_ARCH_I686_DEVICES_PIT_H */
