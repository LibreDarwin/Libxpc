#include "xpc_internal.h"

xpc_endpoint_t
xpc_get_attachment_endpoint(xpc_object_t object)
{
    (void)object;
    return (xpc_endpoint_t)NULL;
}

void *
xpc_get_class4NSXPC(xpc_object_t object)
{
    (void)object;
    return NULL;
}

const char *
xpc_get_event_name(xpc_object_t object)
{
    (void)object;
    return NULL;
}

xpc_object_t
xpc_get_instance(const char *service_name)
{
    (void)service_name;
    return NULL;
}

const char *
xpc_get_service_name_from_pid(pid_t pid)
{
    (void)pid;
    return NULL;
}

uid_t
xpc_get_service_uid_for_token(audit_token_t token)
{
    (void)token;
    /* return (uid_t)-1 if invalid */
    return (uid_t)-1;
}
