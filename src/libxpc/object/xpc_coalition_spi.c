#include "xpc_internal.h"
#include <mach/mach.h>

xpc_object_t
xpc_coalition_copy_info(int type)
{
    (void)type;
    return NULL;
}

mach_port_t
xpc_coalition_history_pipe_async(xpc_object_t object, int p2)
{
    (void)object;
    (void)p2;
    return MACH_PORT_NULL;
}
