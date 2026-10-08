#include "xpc_internal.h"
#include <stddef.h>

xpc_object_t
xpc_string_cache_create(void)
{
    return NULL;
}

void
xpc_string_cache_for_each(xpc_object_t cache, void (^block)(const char *string, size_t length))
{
    (void)cache;
    (void)block;
}

size_t
xpc_string_cache_get_count(xpc_object_t cache)
{
    (void)cache;
    return 0;
}

const char *
xpc_string_cache_get_name(xpc_object_t cache)
{
    (void)cache;
    return NULL;
}

xpc_object_t
xpc_string_create_cached(const char *string)
{
    (void)string;
    return NULL;
}
