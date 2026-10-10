#define LOG_DOMAIN "timer"

#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/timer.h>

#include <libalgo/linked_list.h>

#include <sys/time.h>

/*
 *
 */
void timer_wait_ms(time_t ms)
{
	const clock_t start = timer_ticks_counter;
	const clock_t end = start + MS_TO_TICKS(ms);

	sched_block_waiting_until(current, end);
}

/*
 *
 */
void timer_delay_ms(time_t us)
{
	const clock_t start = timer_ticks_counter;
	const clock_t end = start + MS_TO_TICKS(us);

	WAIT_FOR(timer_ticks_counter >= end);
}

/*
 * Arch-specific implementatoin of timer_start().
 */
extern error_t arch_timer_start(u32 frequency);

/*
 *
 */
void timer_start(u32 frequency)
{
	error_t err;

	err = arch_timer_start(frequency);
	if (err)
		PANIC("Failed to start kernel timer.");
}

/*
 * alarm() syscall - TODO: remove stub.
 */
unsigned sys_alarm(unsigned seconds)
{
	(void)seconds;

	return 0;
}

/*
 * gettimeofday() syscall - get current date and time.
 */
int sys_gettimeofday(struct timeval *tv, void *tz)
{
	struct timespec now;

	UNUSED(tz);

	clock_get_time(&now);

	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec / 1000;

	return 0;
}
