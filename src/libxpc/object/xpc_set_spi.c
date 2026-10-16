#include "xpc_internal.h"
#include <dispatch/dispatch.h>

typedef void (^xpc_idle_handler_t)(void);

void
xpc_set_event(xpc_object_t event)
{
    (void)event;
}

void
xpc_set_event_with_flags(const char *stream, const char *name, xpc_object_t descriptor, uint64_t flags)
{
    (void)stream; (void)name; (void)descriptor; (void)flags;
}

void
xpc_set_idle_handler(xpc_idle_handler_t handler, dispatch_queue_t queue)
{
    (void)handler;
    (void)queue;
}
