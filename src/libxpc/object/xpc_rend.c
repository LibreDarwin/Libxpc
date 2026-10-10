#include "xpc_internal.h"
#include <mach/mach.h>

mach_port_t xpc_service_get_rendezvous_token(void *service)
{
    (void)service;
    return MACH_PORT_NULL;
}
