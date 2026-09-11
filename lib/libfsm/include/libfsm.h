/*
 * Very basic state machine library.
 */

#ifndef _LIBFSM_H
#define _LIBFSM_H

#include <stddef.h>

struct fsm;

struct fsm_state {
	void (*state_enter)(struct fsm *fsm, void *data, unsigned int prev_state);
	void (*state_run)(struct fsm *fsm, void *data);
	void (*state_exit)(struct fsm *fsm, void *data, unsigned int next_state);
};

void fsm_init(struct fsm *, struct fsm_state *, size_t state_count, unsigned int init_state);
void fsm_set_data(struct fsm *fsm, void *data);
void fsm_set_state(struct fsm *fsm, unsigned int state);
void fsm_run(struct fsm *fsm);

#endif /* _LIBFSM_H */
