#include "xpc_internal.h"

bool
xpc_data_get_bytes_ptr_and_length(xpc_object_t xdata, void **ptr, size_t *length)
{
    (void)xdata;
    if (ptr) *ptr = NULL;
    if (length) *length = 0;
    return false;
}

size_t
xpc_data_get_inline_max(size_t data_length)
{
    (void)data_length;
    return 0;
}
