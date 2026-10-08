#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_load_jetsam_properties_path(const char *path)
{
    (void)path;
    return KERN_FAILURE;
}

kern_return_t
launch_load_mounted_jetsam_properties(const char *path)
{
    (void)path;
    return KERN_FAILURE;
}
