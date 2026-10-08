#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_add_external_service(const char *service_name)
{
    (void)service_name;
    return KERN_FAILURE;
}
