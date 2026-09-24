#define LOG_DOMAIN "timer"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>

#include <libalgo/linked_list.h>

/*
 * Timeout callback.
 *
 * Pending callbacks are placed inside the @pending_timeouts list,
 * waiting for their timeout to be reached.
 *
 * All callbacks are executed inside a separate thread. This thread is woken up
 * when at least one pending timeout has been reached. Expired timeouts stay in
 * the pending queue until the thread is scheduled again (which might inquire a
 * small delay, at most a few ms).
 *
 * NOTE: Timeout callbacks are called in an interruptible and pre-emptible context.
 */
struct timeout {
	node_t this;
	node_t this_expired;
	u32 duration_ms;
	clock_t timeout;
	timeout_cb_t cb;
	void *data;
	bool pending; /* Inside the pending queue. */
	refcnt_t refcnt;
};

static volatile clock_t earliest_timeout;
static DECLARE_LLIST(pending_timeouts);
static DECLARE_SPINLOCK(pending_timeouts_lock);
struct thread *timeout_callback_thread;

static inline struct timeout *to_timeout(const node_t *node)
{
	return container_of(node, struct timeout, this);
}

/*
 * Comparison function used to sort the list of pending timeouts.
 */
static int timeout_compare(const void *left_node, const void *right_node)
{
	struct timeout *left = to_timeout(left_node);
	struct timeout *right = to_timeout(right_node);

	return left->timeout - right->timeout;
}

/*
 * Add timeout to the pending queue.
 */
static void timeout_add_pending(struct timeout *timeout)
{
	timeout->pending = true;
	llist_insert_sorted(&pending_timeouts, &timeout->this, timeout_compare);
	earliest_timeout = to_timeout(llist_first(&pending_timeouts))->timeout;
}

/*
 * Remove timeout from the pending queue.
 */
static void timeout_remove_pending(struct timeout *timeout)
{
	ASSERT(spinlock_is_held(&pending_timeouts_lock));

	timeout->pending = false;
	llist_remove(&timeout->this);

	/* recompute earliest timeout if removing the old earliest. */
	if (!llist_is_empty(&pending_timeouts))
		earliest_timeout = to_timeout(llist_first(&pending_timeouts))->timeout;
	else
		earliest_timeout = 0;
}

/*
 * Allocate and initialize a new timeout callback.
 */
struct timeout *timeout_new(timeout_cb_t cb, void *data)
{
	struct timeout *t;

	t = kcalloc(1, sizeof(*t), KMALLOC_KERNEL);
	if (!t)
		return NULL;

	t->cb = cb;
	t->data = data;
	refcnt_init(&t->refcnt);
	INIT_LLIST_NODE(t->this);
	INIT_LLIST_NODE(t->this_expired);

	return t;
}

/*
 * Free a timeout callback.
 */
void timeout_destroy(struct timeout *timeout)
{
	locked_scope (&pending_timeouts_lock) {
		/* sync'd with timeout_execute_callbacks() */
		if (refcnt_put(&timeout->refcnt))
			return;
		if (timeout->pending)
			timeout_remove_pending(timeout);
	}

	llist_remove(&timeout->this_expired);

	kfree(timeout);
}

/*
 * Configure the timeout's duration and add it to the list of pending timeouts.
 *
 * NOTE: The timeout is re-armed if it was already running.
 */
void timeout_arm(struct timeout *timeout, u32 ms)
{
	clock_t now = timer_ticks_counter;

	locked_scope (&pending_timeouts_lock) {
		/* re-arm timeout */
		if (timeout->pending)
			timeout_remove_pending(timeout);

		timeout->duration_ms = ms;
		timeout->timeout = now + MS_TO_TICKS(ms);
		if (timeout->timeout == 0)
			timeout->timeout = 1;

		timeout_add_pending(timeout);
	}
}

/*
 * Disable the timeout.
 */
void timeout_cancel(struct timeout *timeout)
{
	locked_scope (&pending_timeouts_lock) {
		if (timeout->pending)
			timeout_remove_pending(timeout);
	}
}

/*
 * Return whether the timeout is running.
 */
bool timeout_is_pending(const struct timeout *timeout)
{
	return timeout->pending;
}

/*
 * Return the amount of time remaining before a timeout (in ms).
 */
u32 timeout_remaining(const struct timeout *timeout)
{
	locked_scope(&pending_timeouts_lock) {
		if (!timeout->pending)
			return 0;

		return TICKS_TO_MS(timeout->timeout - timer_ticks_counter);
	}

	assert_not_reached();
}

/*
 *
 */
static void timeout_execute_callbacks(void *data)
{
	llist_t expired;
	struct timeout *timeout;
	struct timeout *next;

	while (true) {
		clock_t prev_timeout = 0;

		sched_block_thread(current);
		INIT_LLIST(expired);

		/*
		 * Pop and store all expired timeout callbacks.
		 */
		spinlock_acquire(&pending_timeouts_lock);
		FOREACH_LLIST_ENTRY_SAFE(timeout, next, &pending_timeouts, this) {
			if (timeout->timeout < prev_timeout)
				break; /* Next timeout overflowed clock_t */

			if (timeout->timeout > timer_ticks_counter) {
				/* FIXME: Race-condition if the next timeout is reached when out of
				 *        the loop but before blocking the callback thread.
				 *
				 * We put a warning here in case it ever happens and causes
				 * a timeout callback to be skipped/delayed because of this.
				 */
				WARN_ON_MSG(timeout->timeout == timer_ticks_counter + 1,
					    "possible race-condition");
				break;
			}

			/* sync with timeout_destroy() */
			refcnt_get(&timeout->refcnt);
			llist_add_tail(&expired, &timeout->this_expired);
			timeout_remove_pending(timeout);
			prev_timeout = timeout->timeout;
		}
		spinlock_release(&pending_timeouts_lock);

		/*
		 * Execute expired timeouts.
		 */
		FOREACH_LLIST_ENTRY_SAFE(timeout, next, &expired, this_expired) {
			timeout->cb(timeout->data);
			timeout_destroy(timeout); /* decrease refcount and free if needed  */
		}
	}
}

/*
 * Increment the timekeeping timer's tick count.
 */
bool timer_tick(void)
{
	clock_t old_ticks = timer_ticks_counter;

	timer_ticks_counter += 1;

	/* Timeout reached, wakeup callback thread.
	 *
	 * NOTE: An earliest_timeout of 0 means that there are no pending timeout.
	 */
	if (earliest_timeout && earliest_timeout == timer_ticks_counter)
		sched_unblock_thread(timeout_callback_thread);

	return old_ticks > timer_ticks_counter;
}

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

	/*
	 * Create a kernel thread dedicated to executing timeout callbacks.
	 */
	timeout_callback_thread = thread_spawn(&kernel_process,
					       timeout_execute_callbacks, NULL, NULL, NULL,
					       THREAD_KERNEL);
	if (!timeout_callback_thread)
		PANIC("Failed to allocate timeout callback thread");
	sched_new_thread(timeout_callback_thread);
}
