#include <stdint.h>
#include "xpc_internal.h"

xpc_object_t
xpc_create_from_ce_der(uint8_t *der, size_t length)
{
    (void)der; (void)length;
    return NULL;
}

xpc_object_t
xpc_create_from_ce_der_with_key(uint8_t *der, size_t length, xpc_connection_t connection)
{
    (void)der; (void)length; (void)connection;
    return NULL;
}

xpc_object_t
xpc_create_from_plist_descriptor(void *descriptor)
{
    (void)descriptor;
    return NULL;
}

xpc_object_t
xpc_create_from_serialization(const void *data, size_t length)
{
    (void)data; (void)length;
    return NULL;
}

xpc_object_t
xpc_create_from_serialization_with_ool(const void *data, size_t length, const void *ool, size_t ool_length, uint64_t flags)
{
    (void)data; (void)length; (void)ool; (void)ool_length; (void)flags;
    return NULL;
}

xpc_object_t
xpc_create_from_serialization_with_string_cache(const void *data, size_t length, void *cache)
{
    (void)data; (void)length; (void)cache;
    return NULL;
}
