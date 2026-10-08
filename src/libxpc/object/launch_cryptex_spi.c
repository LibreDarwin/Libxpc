#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_cryptex_terminate(audit_token_t token)
{
    (void)token;
    return KERN_FAILURE;
}
