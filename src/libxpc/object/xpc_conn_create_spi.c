#include "xpc_internal.h"

xpc_connection_t
xpc_connection_create_bs_service_listener(const char *service_name, const char *queue_name, xpc_handler_t handler)
{
    (void)service_name; (void)queue_name; (void)handler;
    return NULL;
}

xpc_connection_t
xpc_connection_create_internal_listener(const char *service_name, const char *queue_name, uint64_t flags, xpc_handler_t handler)
{
    (void)service_name; (void)queue_name; (void)flags; (void)handler;
    return NULL;
}

xpc_connection_t
xpc_connection_create_listener(const char *service_name, const char *queue_name, uint64_t flags, xpc_handler_t handler)
{
    (void)service_name; (void)queue_name; (void)flags; (void)handler;
    return NULL;
}
