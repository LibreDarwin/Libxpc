#include "xpc_internal.h"
#include "../include/xpc_private.h"
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

extern xpc_object_t xpc_copy_entitlements_data_for_pid(int pid);

/*
 * xpc_runtime_get_entitlements_data() — the process's undecoded entitlements
 * blob as an xpc_data object, cached for the lifetime of the process.  Apple
 * fills the same global from __xpc_copy_entitlements_data(getpid(), 0, 0x10).
 */
void *
xpc_runtime_get_entitlements_data(void)
{
    static xpc_object_t entitlements_data;
    static bool inited;
    if (!inited) {
        entitlements_data = xpc_copy_entitlements_data_for_pid(getpid());
        inited = true;
    }
    return entitlements_data;
}

void *
xpc_runtime_get_self_entitlements(void)
{
    static xpc_object_t entitlements;
    static bool inited;
    if (!inited) {
        entitlements = xpc_copy_entitlements_for_pid(getpid());
        inited = true;
    }
    return entitlements;
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
