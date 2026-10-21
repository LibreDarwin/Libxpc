#include "xpc_internal.h"
#include "xpc_private.h"
#include <mach/mach.h>

/*
 * Apple constructs {type: 1, handle: 0, cid: <id>} and issues domain routine
 * 0x33d.  A non-zero return maps the reply to NULL; the domain routine plane
 * unwraps "error" itself.  A zero id is answered locally with a null object.
 */
xpc_object_t
xpc_coalition_copy_info(int type)
{
    if (type == 0) {
        return xpc_null_create();
    }

    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(request, "type", 1);
    xpc_dictionary_set_uint64(request, "handle", 0);
    xpc_dictionary_set_uint64(request, "cid", (uint64_t)type);

    xpc_object_t reply = NULL;
    int rc = xpc_domain_routine(XPC_ROUTINE_COALITION_INFO, request, &reply);
    xpc_release(request);
    if (rc != 0) {
        reply = NULL;
    }
    return reply;
}

/*
 * Apple issues domain routine 0x33e asynchronously with {type: 1, handle: 0}
 * and the caller-provided object as the async send target.  The async domain
 * plane is not implemented in this port; keep the neutral failure result.
 */
mach_port_t
xpc_coalition_history_pipe_async(xpc_object_t object, int p2)
{
    (void)object;
    (void)p2;
    return MACH_PORT_NULL;
}
