#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <mach/mach.h>
#include "xpc_internal.h"

/*
 * xpc_connection_get_audit_token() — copy the peer's audit token captured
 * from the first received message trailer into *token.  Apple reads the
 * five-word token guarded by the connection's lock; a connection that has
 * not yet seen a peer message yields a zeroed token.
 */
void
xpc_connection_get_audit_token(xpc_connection_t connection, audit_token_t *token)
{
    if (token == NULL) {
        return;
    }
    if (connection == NULL ||
        !XPC_OBJECT_CHECK(connection, &_xpc_type_connection)) {
        memset(token, 0, sizeof(*token));
        return;
    }
    struct _xpc_connection_s *c =
        (struct _xpc_connection_s *)(void *)connection;
    pthread_mutex_lock(&c->lock);
    *token = c->peer_audit;
    pthread_mutex_unlock(&c->lock);
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
