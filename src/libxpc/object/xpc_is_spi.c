#include "xpc_internal.h"

bool
xpc_is_kind_of_xpc_object4NSXPC(void *ptr)
{
    (void)ptr;
    return false;
}

bool
xpc_is_system_session(xpc_connection_t connection)
{
    (void)connection;
    return false;
}

bool
xpc_is_xpcservice(void)
{
    return false;
}
