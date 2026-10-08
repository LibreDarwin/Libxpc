#include "xpc_internal.h"
#include <mach/message.h>

xpc_object_t
xpc_receive_mach_msg(mach_msg_header_t *msg)
{
    (void)msg;
    return NULL;
}

int
xpc_receive_mach_msg_validate_hdr(mach_msg_header_t *msg)
{
    (void)msg;
    return 0;
}

void
xpc_receive_remote_msg(xpc_object_t object)
{
    (void)object;
}
