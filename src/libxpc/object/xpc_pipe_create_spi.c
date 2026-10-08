#include <mach/mach.h>
#include "xpc_internal.h"

xpc_pipe_t
xpc_pipe_create(const char *name, uint64_t flags)
{
    (void)name; (void)flags;
    return NULL;
}

xpc_pipe_t
xpc_pipe_create_reply_from_port(mach_port_t port)
{
    (void)port;
    return NULL;
}

xpc_pipe_t
xpc_pipe_create_with_user_session_uid(uint64_t uid)
{
    (void)uid;
    return NULL;
}
