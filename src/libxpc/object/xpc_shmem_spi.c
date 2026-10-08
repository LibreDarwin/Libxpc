#include "xpc_internal.h"
#include <mach/mach.h>
#include <stddef.h>

xpc_object_t
xpc_shmem_create_readonly(void *buffer, size_t length)
{
    (void)buffer;
    (void)length;
    return NULL;
}

size_t
xpc_shmem_get_length(xpc_object_t object)
{
    (void)object;
    return 0;
}

mach_port_t
xpc_shmem_get_mach_port(xpc_object_t object)
{
    (void)object;
    return MACH_PORT_NULL;
}
