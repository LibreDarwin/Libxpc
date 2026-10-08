#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_destroy_persona(const char *persona_name)
{
    (void)persona_name;
    return KERN_FAILURE;
}
