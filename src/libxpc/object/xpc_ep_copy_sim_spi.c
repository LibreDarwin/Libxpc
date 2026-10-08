#include <mach/mach.h>
#include "xpc_internal.h"

mach_port_t
xpc_endpoint_copy_listener_port_4sim(xpc_endpoint_t endpoint)
{
    (void)endpoint;
    return MACH_PORT_NULL;
}
