#include <mach/mach.h>
#include "xpc_internal.h"

mach_port_t
xpc_fd_get_port(xpc_object_t fd)
{
    (void)fd;
    return MACH_PORT_NULL;
}
