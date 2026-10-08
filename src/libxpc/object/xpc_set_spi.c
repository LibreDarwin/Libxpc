#include "xpc_internal.h"
#include <dispatch/dispatch.h>

typedef void (^xpc_idle_handler_t)(void);

void
_xpc_set_event(xpc_object_t event)
{
    (void)event;
}

void
_xpc_set_event_state(uint64_t state)
{
    (void)state;
}

void
_xpc_set_event_with_flags(const char *name, const char *value, uint64_t flags)
{
    (void)name;
    (void)value;
    (void)flags;
}

void
_xpc_set_idle_handler(xpc_idle_handler_t handler, dispatch_queue_t queue)
{
    (void)handler;
    (void)queue;
}
