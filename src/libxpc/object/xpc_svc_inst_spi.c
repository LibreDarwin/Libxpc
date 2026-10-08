#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

int
xpc_service_instance_dup2(xpc_object_t instance, int fd, int target_fd)
{
    (void)instance; (void)fd; (void)target_fd;
    return -1;
}

xpc_object_t
xpc_service_instance_get_context(xpc_object_t instance)
{
    (void)instance;
    return NULL;
}

pid_t
xpc_service_instance_get_host_pid(xpc_object_t instance)
{
    (void)instance;
    return 0;
}

pid_t
xpc_service_instance_get_pid(xpc_object_t instance)
{
    (void)instance;
    return 0;
}

xpc_object_t
xpc_service_instance_get_type(void)
{
    return NULL;
}

bool
xpc_service_instance_is_configurable(xpc_object_t instance)
{
    (void)instance;
    return false;
}

int
xpc_service_instance_run(xpc_object_t instance)
{
    (void)instance;
    return -1;
}

void
xpc_service_instance_set_archpref(xpc_object_t instance, const char *archpref)
{
    (void)instance; (void)archpref;
}

void
xpc_service_instance_set_binpref(xpc_object_t instance, const char *binpref)
{
    (void)instance; (void)binpref;
}

void
xpc_service_instance_set_context(xpc_object_t instance, xpc_object_t context)
{
    (void)instance; (void)context;
}

void
xpc_service_instance_set_cwd(xpc_object_t instance, const char *cwd)
{
    (void)instance; (void)cwd;
}

void
xpc_service_instance_set_endpoint(xpc_object_t instance, xpc_endpoint_t endpoint)
{
    (void)instance; (void)endpoint;
}

void
xpc_service_instance_set_environment(xpc_object_t instance, xpc_object_t env)
{
    (void)instance; (void)env;
}

void
xpc_service_instance_set_finalizer_f(xpc_object_t instance, xpc_finalizer_t finalizer)
{
    (void)instance; (void)finalizer;
}

void
xpc_service_instance_set_jetsam_properties(xpc_object_t instance, xpc_object_t props)
{
    (void)instance; (void)props;
}

void
xpc_service_instance_set_path(xpc_object_t instance, const char *path)
{
    (void)instance; (void)path;
}

void
xpc_service_instance_set_start_suspended(xpc_object_t instance, bool suspended)
{
    (void)instance; (void)suspended;
}

void
xpc_service_instance_set_use_sec_transition_shims(xpc_object_t instance, bool use)
{
    (void)instance; (void)use;
}
