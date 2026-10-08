#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

boolean_t
launch_get_system_service_enabled(const char *service)
{
    (void)service;
    return false;
}
