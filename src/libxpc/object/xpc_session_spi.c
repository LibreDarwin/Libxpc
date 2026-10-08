#include "xpc_internal.h"
#include <bsm/audit.h>

xpc_object_t
xpc_session_create_from_connection_4SWIFT(xpc_connection_t connection)
{
    (void)connection;
    return NULL;
}

xpc_object_t
xpc_session_create_xpc_endpoint(xpc_object_t session)
{
    (void)session;
    return NULL;
}

xpc_connection_t
xpc_session_extract_connection_4SWIFT(xpc_object_t session)
{
    (void)session;
    return (xpc_connection_t)NULL;
}

audit_token_t
xpc_session_get_peer_audit_token_4SWIFT(xpc_object_t session)
{
    (void)session;
    audit_token_t t = {0};
    return t;
}

int
xpc_session_set_instance(xpc_object_t session, xpc_object_t instance)
{
    (void)session;
    (void)instance;
    return 0;
}

void
xpc_session_set_target_user_session_uid(xpc_object_t session, uid_t uid)
{
    (void)session;
    (void)uid;
}
