#include "xpc_internal.h"
#include <stddef.h>

void *
xpc_runtime_get_entitlements_data(void)
{
    return NULL;
}

void *
xpc_runtime_get_self_entitlements(void)
{
    return NULL;
}

bool
xpc_runtime_is_app_sandboxed(void)
{
    return false;
}

bool
xpc_runtime_process_has_entered_sandbox(void)
{
    return false;
}
