/* Kernel pattern: state machine with function pointers */
#include <linux/types.h>
#include <linux/kernel.h>

enum device_state {
    STATE_INIT,
    STATE_IDLE,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_ERROR,
    STATE_SHUTDOWN,
    STATE_COUNT
};

enum device_event {
    EVENT_START,
    EVENT_STOP,
    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_ERROR,
    EVENT_RESET,
    EVENT_COUNT
};

struct state_machine;

typedef enum device_state (*transition_fn)(struct state_machine *sm,
                                           enum device_event event);

struct state_machine {
    enum device_state current_state;
    unsigned long transitions;
    unsigned long errors;
    transition_fn handlers[STATE_COUNT];
};

static enum device_state handle_init(struct state_machine *sm,
                                     enum device_event event)
{
    (void)sm;
    if (event == EVENT_START)
        return STATE_IDLE;
    return STATE_INIT;
}

static enum device_state handle_idle(struct state_machine *sm,
                                     enum device_event event)
{
    (void)sm;
    switch (event) {
    case EVENT_START:  return STATE_RUNNING;
    case EVENT_ERROR:  return STATE_ERROR;
    default:           return STATE_IDLE;
    }
}

static enum device_state handle_running(struct state_machine *sm,
                                        enum device_event event)
{
    (void)sm;
    switch (event) {
    case EVENT_STOP:   return STATE_IDLE;
    case EVENT_PAUSE:  return STATE_PAUSED;
    case EVENT_ERROR:  return STATE_ERROR;
    default:           return STATE_RUNNING;
    }
}

static enum device_state handle_paused(struct state_machine *sm,
                                       enum device_event event)
{
    (void)sm;
    switch (event) {
    case EVENT_RESUME: return STATE_RUNNING;
    case EVENT_STOP:   return STATE_IDLE;
    case EVENT_ERROR:  return STATE_ERROR;
    default:           return STATE_PAUSED;
    }
}

static enum device_state handle_error(struct state_machine *sm,
                                      enum device_event event)
{
    sm->errors++;
    if (event == EVENT_RESET)
        return STATE_INIT;
    return STATE_ERROR;
}

static void sm_init(struct state_machine *sm)
{
    sm->current_state = STATE_INIT;
    sm->transitions = 0;
    sm->errors = 0;
    sm->handlers[STATE_INIT]    = handle_init;
    sm->handlers[STATE_IDLE]    = handle_idle;
    sm->handlers[STATE_RUNNING] = handle_running;
    sm->handlers[STATE_PAUSED]  = handle_paused;
    sm->handlers[STATE_ERROR]   = handle_error;
    sm->handlers[STATE_SHUTDOWN] = NULL;
}

static int sm_process(struct state_machine *sm, enum device_event event)
{
    transition_fn handler;
    enum device_state new_state;

    if (sm->current_state >= STATE_COUNT)
        return -1;

    handler = sm->handlers[sm->current_state];
    if (!handler)
        return -1;

    new_state = handler(sm, event);
    if (new_state != sm->current_state) {
        sm->current_state = new_state;
        sm->transitions++;
    }
    return 0;
}

void test_state_machine(void)
{
    struct state_machine sm;

    sm_init(&sm);
    sm_process(&sm, EVENT_START);
    sm_process(&sm, EVENT_START);
    sm_process(&sm, EVENT_PAUSE);
    sm_process(&sm, EVENT_RESUME);
    sm_process(&sm, EVENT_ERROR);
    sm_process(&sm, EVENT_RESET);
}
