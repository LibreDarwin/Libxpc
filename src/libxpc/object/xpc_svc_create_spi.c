#include "xpc_internal.h"

xpc_object_t
xpc_service_create(const char *service_name, const char *service_domain)
{
    (void)service_name; (void)service_domain;
    return NULL;
}

xpc_object_t
xpc_service_create_from_specifier(xpc_connection_t connection, xpc_object_t specifier)
{
    (void)connection; (void)specifier;
    return NULL;
}
