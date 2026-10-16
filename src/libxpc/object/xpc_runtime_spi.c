#include "xpc_internal.h"
#include <stddef.h>
#include <stdlib.h>

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
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("APP_SANDBOX_CONTAINER_ID") != NULL) ? 1 : 0;
    }
    return cached != 0;
}

bool
xpc_runtime_process_has_entered_sandbox(void)
{
    return false;
}
