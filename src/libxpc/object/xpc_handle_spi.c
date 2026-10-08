#include "xpc_internal.h"

xpc_object_t
xpc_handle_service(const char *name, void *handle, xpc_session_t session, xpc_object_t error)
{
    (void)name; (void)handle; (void)session; (void)error;
    return NULL;
}

xpc_object_t
xpc_handle_subservice(const char *name, const char *subservice_name, void *handle, xpc_session_t session, xpc_object_t error)
{
    (void)name; (void)subservice_name; (void)handle; (void)session; (void)error;
    return NULL;
}
