#include "xpc_internal.h"

kern_return_t
xpc_pipe_interface_routine_async(xpc_pipe_t pipe, xpc_object_t request)
{
    (void)pipe; (void)request;
    return KERN_FAILURE;
}

kern_return_t
xpc_pipe_interface_simpleroutine(xpc_pipe_t pipe, xpc_object_t request)
{
    (void)pipe; (void)request;
    return KERN_FAILURE;
}
