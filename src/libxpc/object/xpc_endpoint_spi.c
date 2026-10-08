#include <mach/mach.h>
#include "xpc_internal.h"

xpc_endpoint_t
xpc_endpoint_create_bs_from_port(mach_port_t port)
{
    (void)port;
    return NULL;
}

xpc_endpoint_t
xpc_endpoint_create_bs_named(const char *name, uint64_t flags)
{
    (void)name; (void)flags;
    return NULL;
}

xpc_endpoint_t
xpc_endpoint_create_bs_named_user(const char *name, uint64_t uid)
{
    (void)name; (void)uid;
    return NULL;
}

xpc_endpoint_t
xpc_endpoint_create_bs_service(const char *name)
{
    (void)name;
    return NULL;
}

xpc_endpoint_t
xpc_endpoint_create_mach_port_4sim(mach_port_t port)
{
    (void)port;
    return NULL;
}
