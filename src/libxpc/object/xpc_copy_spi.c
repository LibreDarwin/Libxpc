/*
 * xpc_copy_spi.c - minimal SPI implementations for xpc_copy_*
 */

#include "xpc_internal.h"

xpc_object_t
xpc_copy_bootstrap(void)
{
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    if (!dict) return NULL;
    xpc_dictionary_set_uint64(dict, "type", 5);
    return dict;
}

xpc_object_t
xpc_copy_domain(const char *name)
{
    if (!name) return xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    if (!dict) return NULL;
    xpc_dictionary_set_string(dict, "name", name);
    return dict;
}

xpc_object_t
xpc_copy_event(const char *stream, const char *name, uint64_t flags, xpc_object_t data)
{
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    if (!dict) return NULL;
    if (stream) xpc_dictionary_set_string(dict, "stream", stream);
    if (name) xpc_dictionary_set_string(dict, "name", name);
    xpc_dictionary_set_uint64(dict, "flags", flags);
    if (data) xpc_dictionary_set_value(dict, "data", data);
    return dict;
}

char *
xpc_copy_short_description(xpc_object_t obj)
{
    /* Use basic description if available */
    char *desc = xpc_copy_description(obj);
    if (!desc) return NULL;
    /* Trim? not critical - just return as is */
    return desc;
}
