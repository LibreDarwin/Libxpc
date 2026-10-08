#include <dispatch/dispatch.h>
#include "xpc_internal.h"

xpc_object_t
xpc_bundle_resolve(const char *path)
{
    (void)path;
    return NULL;
}

typedef void (*_xpc_bundle_resolve_handler_t)(xpc_object_t, const char *, xpc_object_t);

xpc_object_t
xpc_bundle_resolve_on_queue(const char *path, dispatch_queue_t queue, void *handler)
{
    (void)path; (void)queue; (void)handler;
    return NULL;
}

xpc_object_t
xpc_bundle_resolve_sync(const char *path, dispatch_queue_t queue, void *handler)
{
    (void)path; (void)queue; (void)handler;
    return NULL;
}
