#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <mach/mach.h>
#include "../include/xpc_private.h"
#include "xpc_internal.h"

xpc_endpoint_t
xpc_get_attachment_endpoint(xpc_object_t object)
{
    (void)object;
    return (xpc_endpoint_t)NULL;
}

/*
 * Apple's xpc_get_class4NSXPC() is literally an empty function: the symbol
 * exists only so NSXPCConnection code can reference it.
 */
void
xpc_get_class4NSXPC(void)
{
}

/*
 * Apple's xpc_get_event_name(): resolve the name of the event identified by
 * `stream`/`token` into `out_name` (a 128-byte buffer).  Returns non-zero on
 * success, zero (and logs) on failure.
 */
int
xpc_get_event_name(const char *stream, uint64_t token, char *out_name)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "stream", stream);
    xpc_dictionary_set_uint64(request, "token", token);
    xpc_object_t reply = NULL;
    int rc = xpc_event_routine(0x192, request, &reply);
    int result = 0;
    if (rc == 0) {
        const char *name = reply ? xpc_dictionary_get_string(reply, "name") : NULL;
        if (name != NULL) {
            strlcpy(out_name, name, 0x80);
            result = 1;
        }
    }
    if (reply) {
        xpc_release(reply);
    }
    xpc_release(request);
    return result;
}

/*
 * Apple's xpc_get_instance(): fill `out_instance` (a 16-byte uuid_t) with
 * the launchd service-instance uuid for the caller's own pid.  The uuid is
 * fetched over the service subsystem (routine 0x2d0) under the "instance"
 * key; a service that has no instance (or is unknown) leaves the buffer
 * untouched.  Returns non-zero on success.
 */
static int
xpc_get_service_instance(pid_t pid, uuid_t out_instance)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t reply = NULL;
    int result;

    xpc_dictionary_set_int64(request, "pid", (int64_t)pid);
    result = xpc_service_routine(XPC_ROUTINE_SERVICE_INSTANCE, request,
        &reply);
    xpc_release(request);
    if (result != 0) {
        /* 0x71: the service exists but has no instance yet. */
        if (reply) {
            xpc_release(reply);
        }
        return 0;
    }
    if (reply == NULL) {
        return 0;
    }
    const uint8_t *instance = xpc_dictionary_get_uuid(reply, "instance");
    if (instance != NULL) {
        uuid_copy(out_instance, instance);
    }
    xpc_release(reply);
    return instance != NULL;
}

void
xpc_get_instance(uuid_t out_instance)
{
    (void)xpc_get_service_instance(getpid(), out_instance);
}

/*
 * Apple's xpc_get_service_name_from_pid(): ask launchd (service subsystem,
 * routine 0x2d5) which named service a pid belongs to.  Returns a malloc'd
 * copy of the name (caller frees) or NULL.
 */
char *
xpc_get_service_name_from_pid(pid_t pid)
{
    if (pid == 0) {
        return NULL;
    }
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(request, "pid", (int64_t)pid);
    xpc_object_t reply = NULL;
    int rc = xpc_service_routine(0x2d5, request, &reply);
    xpc_release(request);
    char *result = NULL;
    if (rc == 0 && reply != NULL) {
        const char *name = xpc_dictionary_get_string(reply, "service-name");
        if (name != NULL) {
            result = strdup(name);
        }
        xpc_release(reply);
    }
    return result;
}

/*
 * Apple's xpc_get_service_uid_for_token(): ask launchd (event subsystem,
 * routine 0x19b) for the uid owning `token`.  A "user" name is resolved via
 * the password database; otherwise the numeric "uid" is used.  Returns
 * (uid_t)-1 on failure.
 */
uid_t
xpc_get_service_uid_for_token(uint64_t token)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(request, "token", token);
    xpc_object_t reply = NULL;
    int rc = xpc_event_routine(0x19b, request, &reply);
    uid_t result = (uid_t)-1;
    if (rc == 0 && reply != NULL) {
        const char *user = xpc_dictionary_get_string(reply, "user");
        if (user == NULL) {
            result = (uid_t)xpc_dictionary_get_uint64(reply, "uid");
        } else {
            struct passwd *pw = getpwnam(user);
            result = pw != NULL ? pw->pw_uid : (uid_t)-1;
        }
        xpc_release(reply);
    }
    xpc_release(request);
    return result;
}
