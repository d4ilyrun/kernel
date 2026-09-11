#include <libfsm.h>

/*
 *
 */
void fsm_init(struct fsm *fsm, const struct fsm_state *states, size_t state_count,
	      unsigned int init_state)
{
	fsm->states = states;
	fsm->cur_state = init_state;
	fsm->state_count = state_count;
}

/*
 * Transition between two states.
 */
void fsm_set_state(struct fsm *fsm, unsigned int state)
{
	if (state >= fsm->state_count)
		return;

	if (fsm->states[fsm->cur_state].state_exit)
		fsm->states[fsm->cur_state].state_exit(fsm, state);

	if (fsm->states[state].state_enter)
		fsm->states[state].state_enter(fsm, fsm->cur_state);
	fsm->cur_state = state;
}

/*
 *
 */
void fsm_run(struct fsm *fsm, void *data)
{
	fsm->states[fsm->cur_state].state_run(fsm, data);
}
