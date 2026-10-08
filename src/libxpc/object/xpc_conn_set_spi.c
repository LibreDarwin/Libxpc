#include <stdint.h>
#include <sys/types.h>
#include "xpc_internal.h"

void
xpc_connection_set_bootstrap(xpc_connection_t connection, xpc_object_t bootstrap)
{
    (void)connection; (void)bootstrap;
}

void
xpc_connection_set_bs_type(xpc_connection_t connection, const char *type)
{
    (void)connection; (void)type;
}

void
xpc_connection_set_distorter(xpc_connection_t connection, int distorter)
{
    (void)connection; (void)distorter;
}

void
xpc_connection_set_event_handler_f(xpc_connection_t connection, xpc_handler_t handler)
{
    (void)connection; (void)handler;
}

void
xpc_connection_set_instance(xpc_connection_t connection, const char *instance)
{
    (void)connection; (void)instance;
}

void
xpc_connection_set_instance_binpref(xpc_connection_t connection, const char *instance)
{
    (void)connection; (void)instance;
}

void
xpc_connection_set_legacy(xpc_connection_t connection)
{
    (void)connection;
}

void
xpc_connection_set_non_launching(xpc_connection_t connection)
{
    (void)connection;
}

void
xpc_connection_set_oneshot_instance(xpc_connection_t connection, const char *instance)
{
    (void)connection; (void)instance;
}

void
xpc_connection_set_privileged(xpc_connection_t connection)
{
    (void)connection;
}

void
xpc_connection_set_qos_class_fallback(xpc_connection_t connection, qos_class_t qos_class)
{
    (void)connection; (void)qos_class;
}

void
xpc_connection_set_qos_class_floor(xpc_connection_t connection, qos_class_t qos_class)
{
    (void)connection; (void)qos_class;
}

void
xpc_connection_set_target_uid(xpc_connection_t connection, uid_t uid)
{
    (void)connection; (void)uid;
}

void
xpc_connection_set_target_user_session_uid(xpc_connection_t connection, uid_t uid)
{
    (void)connection; (void)uid;
}
