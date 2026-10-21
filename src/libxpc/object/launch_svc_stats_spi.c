#include "../include/xpc_private.h"
#include <stdint.h>
#include "xpc_internal.h"

#define XPC_SERVICE_STATS_DEFAULT_MONITOR "com.apple.launchd.service-stats-default"
#define XPC_SERVICE_STATS_DEFAULT_CAPACITY 0x1000

/*
 * Service statistics control SPI.  These build a launchd *control* request
 * (subsystem 8) carrying a "monitor_id" and dispatch it through the domain
 * control routine; the reply to the is_enabled query carries an "enabled"
 * boolean.
 */

int
launch_service_stats_disable(const char *label)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "monitor_id", label);

    xpc_object_t reply = NULL;
    int rc = xpc_control_routine(XPC_CONTROL_SERVICE_STATS_DISABLE, request,
        &reply);
    if (reply) {
        xpc_release(reply);
    }
    xpc_release(request);
    return rc;
}

int
launch_service_stats_disable_4ppse(void)
{
    return launch_service_stats_disable(XPC_SERVICE_STATS_DEFAULT_MONITOR);
}

int
launch_service_stats_enable(const char *label, uint32_t capacity)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "monitor_id", label);
    xpc_dictionary_set_uint64(request, "capacity", capacity);

    xpc_object_t reply = NULL;
    int rc = xpc_control_routine(XPC_CONTROL_SERVICE_STATS_ENABLE, request,
        &reply);
    if (reply) {
        xpc_release(reply);
    }
    xpc_release(request);
    return rc;
}

int
launch_service_stats_enable_4ppse(void)
{
    return launch_service_stats_enable(XPC_SERVICE_STATS_DEFAULT_MONITOR,
        XPC_SERVICE_STATS_DEFAULT_CAPACITY);
}

boolean_t
launch_service_stats_is_enabled(const char *label)
{
    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(request, "monitor_id", label);

    xpc_object_t reply = NULL;
    int rc = xpc_control_routine(XPC_CONTROL_SERVICE_STATS_IS_ENABLED, request,
        &reply);
    boolean_t enabled = false;
    if (rc == 0) {
        enabled = xpc_dictionary_get_bool(reply, "enabled");
    }
    if (reply) {
        xpc_release(reply);
    }
    xpc_release(request);
    return enabled;
}

boolean_t
launch_service_stats_is_enabled_4ppse(void)
{
    return launch_service_stats_is_enabled(XPC_SERVICE_STATS_DEFAULT_MONITOR);
}
