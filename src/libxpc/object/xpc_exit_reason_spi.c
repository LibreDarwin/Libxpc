#include <stddef.h>
#include <stdint.h>
#include "xpc_internal.h"

/*
 * Apple routes this through the generic __xpc_enum2label(code, table, count),
 * which walks a {int32 code; const char *label;} table of `count` entries and
 * returns the matching label or NULL.  The table is local to libxpc (never
 * exported), so it is inlined here.
 */
static const struct {
    int32_t code;
    const char *label;
} xpc_exit_reason_labels[] = {
    { 1, "XPC_EXIT_REASON_RESERVED" },
    { 2, "XPC_EXIT_REASON_UNKNOWN_IPC" },
    { 3, "XPC_EXIT_REASON_EXTENSION_CHECK_IN_TIMEOUT" },
    { 4, "XPC_EXIT_REASON_SIGTERM_TIMEOUT" },
    { 5, "XPC_EXIT_REASON_ABANDONED" },
    { 6, "XPC_EXIT_REASON_SANDBOX_FILTERED_IPC" },
    { 7, "XPC_EXIT_REASON_FAST_LOGOUT" },
    { 8, "XPC_EXIT_REASON_UNMANAGED" },
    { 9, "XPC_EXIT_REASON_FAULT" },
};

const char *
xpc_exit_reason_get_label(xpc_object_t reason)
{
    int64_t code = (int64_t)(uintptr_t)reason;

    for (size_t i = 0; i < sizeof(xpc_exit_reason_labels) /
        sizeof(xpc_exit_reason_labels[0]); i++) {
        if (code == xpc_exit_reason_labels[i].code) {
            return xpc_exit_reason_labels[i].label;
        }
    }
    return NULL;
}
