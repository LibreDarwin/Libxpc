#include "xpc_internal.h"
#include <stdint.h>

xpc_object_t
xpc_date_create_absolute(int64_t value)
{
    (void)value;
    return NULL;
}

int64_t
xpc_date_get_value_absolute(xpc_object_t object)
{
    (void)object;
    return 0;
}

bool
xpc_date_is_int64_range(xpc_object_t object)
{
    (void)object;
    return false;
}
