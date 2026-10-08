#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

xpc_object_t
launch_service_instance_copy_uuids(void)
{
    return NULL;
}

kern_return_t
launch_service_instance_create(xpc_object_t job, xpc_object_t dictionary, xpc_object_t *instance)
{
    (void)job; (void)dictionary;
    if (instance) *instance = NULL;
    return KERN_FAILURE;
}

kern_return_t
launch_service_instance_remove(xpc_object_t instance)
{
    (void)instance;
    return KERN_FAILURE;
}
