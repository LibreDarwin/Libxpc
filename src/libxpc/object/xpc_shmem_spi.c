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
    if (object == NULL || !XPC_OBJECT_CHECK(object, &_xpc_type_shmem)) {
        return 0;
    }
    return (size_t)XPC_CAST(xpc_shmem_t, object)->size;
}

mach_port_t
xpc_shmem_get_mach_port(xpc_object_t object)
{
    if (object == NULL || !XPC_OBJECT_CHECK(object, &_xpc_type_shmem)) {
        return MACH_PORT_NULL;
    }
    return XPC_CAST(xpc_shmem_t, object)->port;
}
