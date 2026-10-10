#include "xpc_internal.h"

static uint64_t _xpc_event_state = 0;

int _xpc_set_event_state_impl(uint64_t state)
{
    _xpc_event_state = state;
    return 0;
}

int xpc_set_event_state(uint64_t state)
{
    return _xpc_set_event_state_impl(state);
}
