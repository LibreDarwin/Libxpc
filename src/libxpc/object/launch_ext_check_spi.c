#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_extension_check_in_live_4UIKit(const char *identifier, const char *instance, const char *foo, const char *bar)
{
    (void)identifier; (void)instance; (void)foo; (void)bar;
    return KERN_FAILURE;
}
