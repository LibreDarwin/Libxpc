#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

xpc_object_t
launch_perfcheck_property_endpoint_active(xpc_object_t ep, xpc_object_t proc)
{
    (void)ep; (void)proc;
    return NULL;
}

xpc_object_t
launch_perfcheck_property_endpoint_event(xpc_object_t ep, xpc_object_t proc, xpc_object_t events)
{
    (void)ep; (void)proc; (void)events;
    return NULL;
}

const char *
launch_perfcheck_property_endpoint_name(xpc_object_t ep)
{
    (void)ep;
    return NULL;
}

int
launch_perfcheck_property_endpoint_needs_activation(xpc_object_t ep)
{
    (void)ep;
    return 0;
}

xpc_object_t
launch_perfcheck_property_endpoints(xpc_object_t job)
{
    (void)job;
    return NULL;
}
