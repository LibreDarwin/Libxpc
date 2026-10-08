#include "xpc_internal.h"

xpc_object_t
xpc_bundle_create(const char *identifier)
{
    (void)identifier;
    return NULL;
}

xpc_object_t
xpc_bundle_create_from_origin(xpc_object_t origin)
{
    (void)origin;
    return NULL;
}

xpc_object_t
xpc_bundle_create_from_origin_with_string_cache(xpc_object_t origin, void *cache)
{
    (void)origin; (void)cache;
    return NULL;
}

xpc_object_t
xpc_bundle_create_main(void)
{
    return NULL;
}

xpc_object_t
xpc_bundle_create_with_string_cache(const char *identifier, void *cache)
{
    (void)identifier; (void)cache;
    return NULL;
}
