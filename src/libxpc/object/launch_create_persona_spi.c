#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

kern_return_t
launch_create_persona(const char *persona_name, const char *persona_type)
{
    (void)persona_name; (void)persona_type;
    return KERN_FAILURE;
}
