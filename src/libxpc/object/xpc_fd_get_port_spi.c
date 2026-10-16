#include <mach/mach.h>
#include "xpc_internal.h"

mach_port_t
xpc_fd_get_port(xpc_object_t fd)
{
    if (fd == NULL || !XPC_OBJECT_CHECK(fd, &_xpc_type_fd)) {
        return MACH_PORT_NULL;
    }
    return XPC_CAST(xpc_fd_t, fd)->port;
}
