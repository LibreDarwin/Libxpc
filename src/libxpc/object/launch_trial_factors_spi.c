#include <sys/types.h>
#include <mach/mach.h>
#include "../include/xpc_private.h"
#include "xpc_internal.h"

extern int _launch_trial_factors_routine(uint64_t routine,
    xpc_object_t request, xpc_object_t *reply);
extern xpc_object_t xpc_dictionary_get_dictionary(xpc_object_t xdict,
    const char *key);

/*
 * launch_trial_factors_active_reload() — ask launchd (trial-factors subsystem,
 * routine 0x514) to reload trial-factor data.  On success the result is the
 * "reload-error" value (0 on success); when non-zero, the retained "errors"
 * dictionary is returned through `errors_out`.
 */
int
launch_trial_factors_active_reload(xpc_object_t *errors_out)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t reply = NULL;
    int rc = _launch_trial_factors_routine(0x514, request, &reply);
    xpc_release(request);
    if (rc == 0) {
        if (reply == NULL) {
            rc = 0x76;
        } else {
            int64_t reload_error = xpc_dictionary_get_int64(reply,
                "reload-error");
            xpc_object_t errors = xpc_dictionary_get_dictionary(reply,
                "errors");
            if (reload_error != 0 && errors_out && errors) {
                *errors_out = xpc_retain(errors);
            }
            xpc_release(reply);
            rc = (int)reload_error;
        }
    }
    return rc;
}

kern_return_t
launch_trial_factors_routine(mach_port_t port, xpc_object_t req, xpc_object_t *resp)
{
    (void)port; (void)req;
    if (resp) *resp = NULL;
    return KERN_FAILURE;
}
