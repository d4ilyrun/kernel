/*
 * Very basic state machine library.
 */

#ifndef _LIBFSM_H
#define _LIBFSM_H

#include <stddef.h>

struct fsm {
	unsigned int cur_state;
	const struct fsm_state *states;
	size_t state_count;
};

struct fsm_state {
	void (*state_run)(struct fsm *fsm, void *data);
	void (*state_enter)(struct fsm *fsm, unsigned int prev_state);
	void (*state_exit)(struct fsm *fsm, unsigned int next_state);
};

void fsm_init(struct fsm *, const struct fsm_state *, size_t state_count, unsigned int init_state);
void fsm_set_state(struct fsm *fsm, unsigned int state);
void fsm_run(struct fsm *fsm, void *data);

#endif /* _LIBFSM_H */
