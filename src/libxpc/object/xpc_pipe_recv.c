#include "xpc_internal.h"
#include <mach/mach.h>

kern_return_t xpc_pipe_receive(xpc_pipe_t pipe, xpc_object_t *message, mach_msg_timeout_t timeout)
{
    (void)pipe; (void)message; (void)timeout;
    return KERN_FAILURE;
}
