#include <stdint.h>
#include <mach/mach.h>
#include "xpc_internal.h"

int
xpc_connection_get_audit_token(xpc_connection_t connection, audit_token_t *token)
{
    (void)connection; (void)token;
    return -1;
}

const char *
xpc_connection_get_bs_type(xpc_connection_t connection)
{
    (void)connection;
    return NULL;
}

uint64_t
xpc_connection_get_filter_policy_id_4test(xpc_connection_t connection)
{
    (void)connection;
    return 0;
}

const char *
xpc_connection_get_instance(xpc_connection_t connection)
{
    (void)connection;
    return NULL;
}

const char *
xpc_connection_get_parent_4test(xpc_connection_t connection)
{
    (void)connection;
    return NULL;
}

const char *
xpc_connection_get_peer_instance(xpc_connection_t connection)
{
    (void)connection;
    return NULL;
}

mach_port_t
xpc_connection_get_recvp_4test(xpc_connection_t connection)
{
    (void)connection;
    return MACH_PORT_NULL;
}
