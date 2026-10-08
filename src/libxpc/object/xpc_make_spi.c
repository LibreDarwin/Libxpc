#include "xpc_internal.h"

xpc_object_t
xpc_make_serialization(xpc_object_t object, void **buffer, size_t *length)
{
    (void)object;
    if (buffer) *buffer = NULL;
    if (length) *length = 0;
    return NULL;
}

xpc_object_t
xpc_make_serialization_with_ool(xpc_object_t object, void **buffer, size_t *length, void **ool_buffer, size_t *ool_length, bool wants_ool)
{
    (void)object;
    (void)ool_buffer;
    (void)ool_length;
    (void)wants_ool;
    if (buffer) *buffer = NULL;
    if (length) *length = 0;
    return NULL;
}
