#include "xpc_internal.h"

xpc_connection_t
xpc_service_attach(const char *service_name, xpc_handler_t handler)
{
    (void)service_name; (void)handler;
    return NULL;
}

xpc_connection_t
xpc_service_attach_with_flags(const char *service_name, xpc_handler_t handler, uint64_t flags)
{
    (void)service_name; (void)handler; (void)flags;
    return NULL;
}
