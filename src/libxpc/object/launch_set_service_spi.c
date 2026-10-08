#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_set_service_enabled(mach_port_t port, const char *service, boolean_t enabled)
{
    (void)port; (void)service; (void)enabled;
    return KERN_FAILURE;
}

kern_return_t
launch_set_system_service_enabled(const char *service, boolean_t enabled)
{
    (void)service; (void)enabled;
    return KERN_FAILURE;
}
