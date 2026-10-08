#include "xpc_internal.h"

kern_return_t
xpc_pipe_routine_async(xpc_pipe_t pipe, xpc_object_t request)
{
    (void)pipe; (void)request;
    return KERN_FAILURE;
}

kern_return_t
xpc_pipe_routine_forward(xpc_pipe_t pipe, xpc_object_t request, mach_port_t reply_port)
{
    (void)pipe; (void)request; (void)reply_port;
    return KERN_FAILURE;
}
