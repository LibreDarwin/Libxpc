#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_trial_factors_active_reload(const char *uuid)
{
    (void)uuid;
    return KERN_FAILURE;
}

kern_return_t
launch_trial_factors_routine(mach_port_t port, xpc_object_t req, xpc_object_t *resp)
{
    (void)port; (void)req;
    if (resp) *resp = NULL;
    return KERN_FAILURE;
}
