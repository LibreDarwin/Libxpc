#include <sys/types.h>
#include <mach/mach.h>
#include "../include/xpc_private.h"
#include "xpc_internal.h"

extern int _launch_job_query_routine(uint64_t routine, xpc_object_t request,
    xpc_object_t *reply);
extern xpc_object_t xpc_dictionary_get_dictionary(xpc_object_t xdict,
    const char *key);

/*
 * launch_job_query_get_additional_job_properties() — ask launchd (job-query
 * subsystem, routine 0x4b0) for the calling job's "additional-properties"
 * dictionary.  The interface result is written through `error_out`; the
 * returned object is retained on success and NULL on failure.
 */
xpc_object_t
launch_job_query_get_additional_job_properties(int *error_out)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t reply = NULL;
    int rc = _launch_job_query_routine(0x4b0, request, &reply);

    xpc_object_t props = NULL;
    if (rc == 0 && reply) {
        props = xpc_dictionary_get_dictionary(reply, "additional-properties");
        if (props) {
            xpc_retain(props);
        }
    }
    if (reply) {
        xpc_release(reply);
    }
    xpc_release(request);

    if (error_out) {
        *error_out = rc;
    }
    return props;
}

kern_return_t
launch_job_query_routine(mach_port_t port, xpc_object_t req, xpc_object_t *resp)
{
    (void)port; (void)req;
    if (resp) *resp = NULL;
    return KERN_FAILURE;
}
