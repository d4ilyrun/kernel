/*
 * Interface with the i8254 PIT.
 *
 * Programmable Interval Counter.
 *
 * Any interaction done with the timer should be done through
 * the functions defined inside this header.
 *
 * Note:
 *
 *   For now, we do not allow for dynamically re-configuring the timer.
 *   We forcefully set it to mode 2 (1 interrupt per tick), and only allow
 *   the user to change its frequency.
 *
 * Reference manual can be found here:
 *   * https://k.lse.epita.fr/data/8254.pdf
 *   * https://k.lse.epita.fr/internals/8254_controller.html
 *   * https://wiki.osdev.org/Programmable_Interval_Timer
 */

#ifndef KERNEL_DEVICES_TIMER_H
#define KERNEL_DEVICES_TIMER_H

#include <utils/types.h>

#define TIMER_INTERNAL_FREQUENCY (1193182)

#define TIMER_TICK_FREQUENCY (1000) // 1KHz

/**
 * Start the timer.
 *
 * At each interval, the timer will trigger an IRQ_TIMER
 * interrupt, which MUST be handled to update our internal
 * timer tick value.
 *
 * @param frequency The timer's frequency (Hz)
 *
 * TODO: Handle the timer IRQ
 */
void timer_start(u32 frequency);

/** Read the current value inside the timer's counter */
u16 timer_read(void);

#endif /* KERNEL_DEVICES_TIMER_H */
