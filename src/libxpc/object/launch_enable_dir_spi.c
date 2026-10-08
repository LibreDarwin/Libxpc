#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_enable_directory(audit_token_t token, const char *path)
{
    (void)token; (void)path;
    return KERN_FAILURE;
}
