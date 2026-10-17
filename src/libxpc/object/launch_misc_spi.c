#include "../include/xpc_private.h"
#include <mach/mach.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * launch_activate_socket() — ask launchd to activate the on-demand socket(s)
 * declared for `name` and hand back the launched file descriptors.  Apple
 * builds a domain activate request (type 6, flags 0x20, routine 0x325),
 * expects a reply carrying an array of file ports under "port", and copies
 * each one out via xpc_array_dup_fd().
 */
int
launch_activate_socket(const char *name, int **fds, size_t *cnt)
{
    if (fds) *fds = NULL;
    if (cnt) *cnt = 0;
    if (!name || !fds || !cnt) {
        return 0x76;
    }

    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(request, "type", 6);
    xpc_dictionary_set_uint64(request, "handle", 0);
    xpc_dictionary_set_uint64(request, "flags", 0x20);
    xpc_dictionary_set_string(request, "name", name);

    xpc_object_t reply = NULL;
    int rc = xpc_domain_routine(XPC_ROUTINE_ACTIVATE, request, &reply);
    if (rc != 0) {
        xpc_release(request);
        return rc;
    }

    xpc_object_t ports = reply ? xpc_dictionary_get_value(reply, "port") : NULL;
    if (!ports || !XPC_OBJECT_CHECK(ports, &_xpc_type_array)) {
        xpc_release(reply);
        xpc_release(request);
        return 0x76;
    }

    size_t n = xpc_array_get_count(ports);
    int *out = malloc((n ? n : 1) * sizeof(int));
    if (!out) {
        xpc_release(reply);
        xpc_release(request);
        return 0x76;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = xpc_array_dup_fd(ports, i);
    }

    *fds = out;
    *cnt = n;
    xpc_release(reply);
    xpc_release(request);
    return 0;
}
