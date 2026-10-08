#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_job_query_get_additional_job_properties(mach_port_t port, int64_t id, xpc_object_t *props)
{
    (void)port; (void)id;
    if (props) *props = NULL;
    return KERN_FAILURE;
}

kern_return_t
launch_job_query_routine(mach_port_t port, xpc_object_t req, xpc_object_t *resp)
{
    (void)port; (void)req;
    if (resp) *resp = NULL;
    return KERN_FAILURE;
}
