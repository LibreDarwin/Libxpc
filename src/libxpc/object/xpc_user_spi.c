#include "xpc_internal.h"

bool
xpc_user_sessions_enabled(void)
{
    return false;
}

uid_t
xpc_user_sessions_get_foreground_uid(void)
{
    return (uid_t)-1;
}

uid_t
xpc_user_sessions_get_session_uid(xpc_connection_t connection)
{
    (void)connection;
    return (uid_t)-1;
}
