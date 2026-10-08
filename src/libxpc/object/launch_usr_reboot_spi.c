#include "xpc_internal.h"

kern_return_t
launch_userspace_reboot_with_purpose(const char *purpose, uint32_t flags)
{
    (void)purpose; (void)flags;
    return KERN_FAILURE;
}
