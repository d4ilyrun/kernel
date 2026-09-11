#include <libfsm.h>

struct fsm {
	void *data;
	unsigned int cur_state;
	struct fsm_state *states;
	size_t state_count;
};

/*
 *
 */
void fsm_init(struct fsm *fsm, struct fsm_state *states, size_t state_count,
	      unsigned int init_state)
{
	fsm->data = NULL;
	fsm->states = states;
	fsm->cur_state = init_state;
	fsm->state_count = state_count;
}

/*
 * Change the object that will be passed to the fsm state callbacks.
 */
void fsm_set_data(struct fsm *fsm, void *data)
{
	fsm->data = data;
}

/*
 * Transition between two states.
 */
void fsm_set_state(struct fsm *fsm, unsigned int state)
{
	if (state >= fsm->state_count)
		return;

	if (fsm->states[fsm->cur_state].state_exit)
		fsm->states[fsm->cur_state].state_exit(fsm, fsm->data, state);

	if (fsm->states[state].state_enter)
		fsm->states[state].state_enter(fsm, fsm->data, fsm->cur_state);
	fsm->cur_state = state;
}

/*
 *
 */
void fsm_run(struct fsm *fsm)
{
	fsm->states[fsm->cur_state].state_run(fsm, fsm->data);
}
