#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

boolean_t
launch_get_service_enabled(mach_port_t port, const char *service)
{
    (void)port; (void)service;
    return false;
}
