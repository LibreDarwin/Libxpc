#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

xpc_object_t
launch_copy_extension_properties(const char *name, const char *domain)
{
    (void)name; (void)domain;
    return NULL;
}

xpc_object_t
launch_copy_extension_properties_for_pid(const char *name, pid_t pid)
{
    (void)name; (void)pid;
    return NULL;
}
