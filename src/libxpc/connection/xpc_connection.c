/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xpc_connection.c — the connection type (xpc/connection.h).
 *
 * A connection models a channel to a peer: a server side (a named
 * listener that allocates a receive right and registers it under its
 * service name) and a client side (which resolves that name to a send
 * right).  Both sides live in the same task in this hermetic build, so
 * the "service registry" is a process-local name-to-send-right table that
 * launchd glue (xpc_connection_register_mach_service) and server-side
 * activation both populate.
 *
 * Delivery is a single daemon receive thread per connection (no
 * libdispatch): suspended connections park the thread and let inbound
 * messages queue in the kernel on recv_right; resume drains them.
 * With-reply requests get a dedicated reply port; the sync and async
 * paths share xpc_pipe_try_receive().
 */

#include "xpc_internal.h"
#include "xpc_private.h"

#include <Block.h>
#include <bsm/libbsm.h>
#include <errno.h>
#include <pthread.h>

#pragma mark - Service registry

/*
 * In-process name → send-right table.  Registered by
 * xpc_connection_register_mach_service() (launchd glue) and by server-side
 * activation of named listeners.  A send right is expected to outlive the
 * table entry for the life of the task, so no per-entry refcounting.
 */
struct registry_entry {
    char        *name;
    mach_port_t port;
};

static pthread_mutex_t g_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct registry_entry *g_registry;
static size_t g_registry_count, g_registry_cap;

static void
registry_set(const char *name, mach_port_t port)
{
    pthread_mutex_lock(&g_registry_lock);
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            g_registry[i].port = port;
            pthread_mutex_unlock(&g_registry_lock);
            return;
        }
    }
    if (g_registry_count == g_registry_cap) {
        size_t newcap = g_registry_cap ? g_registry_cap * 2 : 8;
        struct registry_entry *np =
            realloc(g_registry, newcap * sizeof(*np));
        if (!np) {
            pthread_mutex_unlock(&g_registry_lock);
            return;
        }
        g_registry = np;
        g_registry_cap = newcap;
    }
    struct registry_entry *e = &g_registry[g_registry_count++];
    e->name = strdup(name);
    e->port = port;
    pthread_mutex_unlock(&g_registry_lock);
}

static mach_port_t
registry_lookup(const char *name)
{
    mach_port_t port = MACH_PORT_NULL;
    pthread_mutex_lock(&g_registry_lock);
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) {
            port = g_registry[i].port;
            break;
        }
    }
    pthread_mutex_unlock(&g_registry_lock);
    return port;
}

void
xpc_connection_register_mach_service(const char *name, mach_port_t port)
{
    if (!name || !port) return;
    registry_set(name, port);
}

#pragma mark - mach helpers

static int
make_send_alias(mach_port_t receive, mach_port_t *alias_out)
{
    /* The kernel only permits a MAKE_SEND right to be inserted under a name
     * that already denotes the port in this task.  Passing the receive-right
     * name itself gives us a coincident send right: the same name is valid as
     * both a send and a receive destination (Apple inserts xc_local_port over
     * xc_local_port identically). */
    kern_return_t kr = mach_port_insert_right(mach_task_self(), receive,
        receive, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) return kr;
    *alias_out = receive;
    return KERN_SUCCESS;
}

static void
destroy_receive(mach_port_t *port_inout)
{
    if (MACH_PORT_VALID(*port_inout)) {
        mach_port_mod_refs(mach_task_self(), *port_inout,
            MACH_PORT_RIGHT_RECEIVE, -1);
        *port_inout = MACH_PORT_NULL;
    }
}

#pragma mark - Connection allocation

static struct _xpc_connection_s *
conn_alloc(void)
{
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)
        xpc_object_alloc(&_xpc_type_connection, sizeof(*c));
    if (!c) return NULL;
    memset((char *)c + sizeof(struct _xpc_object_s), 0,
        sizeof(*c) - sizeof(struct _xpc_object_s));
    if (pthread_mutex_init(&c->lock, NULL) != 0) {
        free(c);
        return NULL;
    }
    pthread_cond_init(&c->cond, NULL);
    c->state = XPC_CONN_STATE_INACTIVE;
    c->resume_count = 0;
    /* Apple returns a reason string for an active connection too. */
    c->invalidation_reason = strdup("connection is active");
    /* Every connection owns a receive right; peers reach it via self_port,
     * and xpc_connection_get_port()/set_connection advertise that alias. */
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &c->recv_right) == KERN_SUCCESS) {
        make_send_alias(c->recv_right, &c->self_port);
    }
    return c;
}

#pragma mark - Send helpers

/* Serialize request and hand it to the peer.  When reply_port is valid the
 * message is a ROUTINE carrying a send-once reply right; otherwise it is a
 * simpleroutine. */
static int
conn_send(struct _xpc_connection_s *c, xpc_object_t message,
    mach_port_t reply_port)
{
    uint32_t msg_id = MACH_PORT_VALID(reply_port)
        ? XPC_PIPE_ID_ROUTINE : XPC_PIPE_ID_SIMPLEROUTINE;

    size_t length = 0;
    uint8_t *bytes = xpc_wire_serialize(message, msg_id, &length);
    if (!bytes) return KERN_INVALID_ARGUMENT;
    mach_msg_header_t *header = (mach_msg_header_t *)(void *)bytes;

    header->msgh_remote_port = c->peer_port;
    header->msgh_local_port = reply_port;
    header->msgh_voucher_port = MACH_PORT_NULL;
    uint8_t local_disp = MACH_PORT_VALID(reply_port)
        ? MACH_MSG_TYPE_MAKE_SEND_ONCE : 0;
    header->msgh_bits = (header->msgh_bits & MACH_MSGH_BITS_COMPLEX) |
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, local_disp);

    mach_msg_return_t result = mach_msg(header, MACH_SEND_MSG,
        header->msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
    free(bytes);
    return (int)result;
}

static void
pending_push(struct _xpc_connection_s *c, xpc_object_t message)
{
    if (c->pending_count == c->pending_cap) {
        size_t newcap = c->pending_cap ? c->pending_cap * 2 : 4;
        xpc_object_t *np = realloc(c->pending, newcap * sizeof(*np));
        if (!np) return;
        c->pending = np;
        c->pending_cap = newcap;
    }
    c->pending[c->pending_count++] = message;
    xpc_retain(message);
}

static void
pending_flush(struct _xpc_connection_s *c)
{
    for (size_t i = 0; i < c->pending_count; i++) {
        if (MACH_PORT_VALID(c->peer_port)) {
            conn_send(c, c->pending[i], MACH_PORT_NULL);
        }
        xpc_release(c->pending[i]);
    }
    c->pending_count = 0;
    free(c->pending);
    c->pending = NULL;
    c->pending_cap = 0;
}

/* Deliver an error object to the event handler if one is installed.
 * Must be called without holding c->lock. */
static void
deliver_error(struct _xpc_connection_s *c, xpc_object_t error)
{
    if (!c->has_handler || !c->handler) return;
    c->handler(error);
}

#pragma mark - Teardown

/* Idempotent teardown shared by xpc_connection_cancel() and requirement
 * violations: park the connection, destroy its receive rights (which also
 * unblocks the rx thread and every async-reply waiter), drop queued
 * messages, and deliver the given error to the event handler. */
static void
conn_invalidate(struct _xpc_connection_s *c, xpc_object_t error)
{
    pthread_mutex_lock(&c->lock);
    if (c->cancelled) {
        pthread_mutex_unlock(&c->lock);
        if (error && error != (xpc_object_t)&_xpc_error_connection_invalid)
            deliver_error(c, error);
        return;
    }
    c->cancelled = true;
    c->shutdown = true;
    if (!c->invalidation_reason) {
        c->invalidation_reason = strdup("Connection invalidated");
    }
    pthread_cond_broadcast(&c->cond);

    destroy_receive(&c->recv_right);
    c->self_port = MACH_PORT_NULL;
    for (size_t i = 0; i < c->reply_count; i++) {
        destroy_receive(&c->reply_ports[i]);
    }
    free(c->reply_ports);
    c->reply_ports = NULL;
    c->reply_count = c->reply_cap = 0;
    for (size_t i = 0; i < c->pending_count; i++) {
        xpc_release(c->pending[i]);
    }
    c->pending_count = 0;
    free(c->pending);
    c->pending = NULL;
    c->pending_cap = 0;
    pthread_mutex_unlock(&c->lock);

    deliver_error(c, error);
}

#pragma mark - Receive thread

static void *
rx_main(void *arg)
{
    struct _xpc_connection_s *c = arg;

    for (;;) {
        pthread_mutex_lock(&c->lock);
        while (!c->shutdown && c->resume_count <= 0) {
            pthread_cond_wait(&c->cond, &c->lock);
        }
        if (c->shutdown) {
            pthread_mutex_unlock(&c->lock);
            break;
        }
        xpc_handler_t handler = c->has_handler ? Block_copy(c->handler) : NULL;
        pthread_mutex_unlock(&c->lock);

        xpc_object_t dict = NULL;
        mach_port_t port = c->recv_right;
        (void)xpc_pipe_try_receive(&port, &dict, NULL, NULL, 65536, 0);
        if (!dict) {
            /* Kernel-side failure (e.g. port destroyed at cancel). */
            if (handler) Block_release(handler);
            continue;
        }

        /* Capture peer identity from the audit trailer the pipe layer
         * stamped onto the dictionary. */
        audit_token_t token;
        if (xpc_dictionary_get_audit_token(dict, &token) == 0) {
            pthread_mutex_lock(&c->lock);
            c->have_peer_audit = true;
            c->peer_audit = token;
            pthread_mutex_unlock(&c->lock);
        }

        /* Peer requirement enforcement: a stored requirement that fails
         * against the received message invalidates the connection. */
        bool mismatch = false;
        pthread_mutex_lock(&c->lock);
        mismatch = c->invalidate_on_requirement_failure &&
            c->peer_requirement &&
            !xpc_peer_requirement_match_received_message(
                c->peer_requirement, dict, NULL);
        pthread_mutex_unlock(&c->lock);
        if (mismatch) {
            pthread_mutex_lock(&c->lock);
            if (!c->invalidation_reason) {
                free(c->invalidation_reason);
                c->invalidation_reason = strdup(
                    "Peer does not satisfy code signing requirement");
            }
            pthread_mutex_unlock(&c->lock);
            conn_invalidate(c,
                (xpc_object_t)&_xpc_error_peer_code_signing_requirement);
            if (handler) Block_release(handler);
            xpc_release(dict);
            continue;
        }

        pthread_mutex_lock(&c->lock);
        bool still_here = !c->shutdown;
        pthread_mutex_unlock(&c->lock);

        if (handler) {
            if (still_here) handler(dict);
            Block_release(handler);
        }
        xpc_release(dict);
    }

    xpc_release((xpc_object_t)c);
    return NULL;
}

#pragma mark - Construction

xpc_connection_t
xpc_connection_create(const char *name, dispatch_queue_t targetq)
{
    /* Named connection: the server side of an XPC service. */
    struct _xpc_connection_s *c = conn_alloc();
    if (!c) return NULL;
    c->name = name ? strdup(name) : NULL;
    c->listener = true;     /* registers name→self_port at activation */
    c->target_queue = targetq;
    return (xpc_connection_t)(void *)c;
}

xpc_connection_t
xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq,
    uint64_t flags)
{
    struct _xpc_connection_s *c = conn_alloc();
    if (!c) return NULL;
    c->name = name ? strdup(name) : NULL;
    c->listener = (flags & XPC_CONNECTION_MACH_SERVICE_LISTENER) != 0;
    c->is_anonymous = (flags & XPC_CONNECTION_MACH_SERVICE_ANONYMOUS) != 0;
    (void)(flags & XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
    c->target_queue = targetq;
    return (xpc_connection_t)(void *)c;
}

xpc_connection_t
xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint)
{
    if (!endpoint || !XPC_OBJECT_CHECK(endpoint, &_xpc_type_endpoint))
        return NULL;
    struct _xpc_endpoint_s *e = (struct _xpc_endpoint_s *)(void *)endpoint;
    struct _xpc_connection_s *c = conn_alloc();
    if (!c) return NULL;
    c->peer_port = e->port;
    c->connected = true;
    return (xpc_connection_t)(void *)c;
}

/* Internal: connect to an existing send right. */
xpc_connection_t
xpc_connection_create_with_port(mach_port_t port, xpc_handler_t handler,
    void *context, xpc_finalizer_t finalizer)
{
    struct _xpc_connection_s *c = conn_alloc();
    if (!c) return NULL;
    c->peer_port = port;
    c->connected = true;
    c->context = context;
    c->finalizer = finalizer;
    if (handler) {
        c->handler = Block_copy(handler);
        c->has_handler = true;
    }
    return (xpc_connection_t)(void *)c;
}

mach_port_t
xpc_connection_get_port(xpc_object_t connection)
{
    if (!XPC_OBJECT_CHECK(connection, &_xpc_type_connection))
        return MACH_PORT_NULL;
    return ((struct _xpc_connection_s *)(void *)connection)->self_port;
}

#pragma mark - Lifecycle

void
xpc_connection_set_target_queue(xpc_connection_t connection,
    dispatch_queue_t targetq)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    c->target_queue = targetq;
}

void
xpc_connection_set_event_handler(xpc_connection_t connection,
    xpc_handler_t handler)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    xpc_handler_t old = c->handler;
    c->handler = handler ? Block_copy(handler) : NULL;
    c->has_handler = handler != NULL;
    if (old) Block_release(old);
}

/* Client-side peer resolve against the registry; returns true when a send
 * right is now established.  Caller must NOT hold c->lock. */
static bool
conn_resolve_peer(struct _xpc_connection_s *c)
{
    bool resolved = false;
    pthread_mutex_lock(&c->lock);
    if (!c->connected && !c->listener && c->name) {
        mach_port_t peer = registry_lookup(c->name);
        if (MACH_PORT_VALID(peer)) {
            c->peer_port = peer;
            c->connected = true;
            resolved = true;
            pending_flush(c);
        }
    }
    pthread_mutex_unlock(&c->lock);
    return resolved;
}

void
xpc_connection_activate(xpc_connection_t connection)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;

    pthread_mutex_lock(&c->lock);
    if (c->state != XPC_CONN_STATE_INACTIVE) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    c->state = XPC_CONN_STATE_ACTIVE;
    if (c->resume_count <= 0) c->resume_count = 1;
    pthread_cond_broadcast(&c->cond);

    /* Server side: advertise under the service name so clients resolve. */
    if (c->listener && c->name && MACH_PORT_VALID(c->self_port)) {
        registry_set(c->name, c->self_port);
    }
    pthread_mutex_unlock(&c->lock);

    if (!c->listener && c->name) {
        bool resolved = conn_resolve_peer(c);
        if (!resolved && !c->is_anonymous) {
            /* No service registered yet -- a later send retries the
             * lookup, so stay connected-less rather than fail now. */
        }
    }

    pthread_mutex_lock(&c->lock);
    if (c->rx_thread_started) {
        pthread_mutex_unlock(&c->lock);
        return;
    }
    c->rx_thread_started = true;
    xpc_retain((xpc_object_t)c);
    pthread_t t;
    if (pthread_create(&t, NULL, rx_main, c) == 0) {
        c->rx_thread = t;
    } else {
        c->rx_thread_started = false;
        xpc_release((xpc_object_t)c);
    }
    pthread_mutex_unlock(&c->lock);
}

void
xpc_connection_suspend(xpc_connection_t connection)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    pthread_mutex_lock(&c->lock);
    if (c->resume_count > 0) c->resume_count--;
    pthread_mutex_unlock(&c->lock);
}

void
xpc_connection_resume(xpc_connection_t connection)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    pthread_mutex_lock(&c->lock);
    c->resume_count++;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);
}

void
xpc_connection_cancel(xpc_connection_t connection)
{
    if (!connection) return;
    conn_invalidate((struct _xpc_connection_s *)(void *)connection,
        (xpc_object_t)&_xpc_error_connection_invalid);
}

#pragma mark - Messaging

void
xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message)
{
    if (!connection || !message) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;

    conn_resolve_peer(c);

    pthread_mutex_lock(&c->lock);
    if (!MACH_PORT_VALID(c->peer_port)) {
        /* No established peer yet.  While the service may still appear (a
         * server registers its port at activation) queue the message; once
         * we are active with no peer the service is absent, so fail. */
        if ((c->name && !c->connected && c->state != XPC_CONN_STATE_ACTIVE)
            || c->invalidate_on_requirement_failure) {
            if (c->state != XPC_CONN_STATE_ACTIVE) {
                pending_push(c, message);
            }
            pthread_mutex_unlock(&c->lock);
            if (c->state == XPC_CONN_STATE_ACTIVE) {
                bool has_handler = c->has_handler;
                if (has_handler) {
                    deliver_error(c,
                        (xpc_object_t)&_xpc_error_connection_invalid);
                }
            }
            return;
        }
        bool has_handler = c->has_handler;
        pthread_mutex_unlock(&c->lock);
        if (has_handler) {
            deliver_error(c,
                (xpc_object_t)&_xpc_error_connection_invalid);
        }
        return;
    }
    pthread_mutex_unlock(&c->lock);

    conn_send(c, message, MACH_PORT_NULL);
}

void
xpc_connection_send_barrier(xpc_connection_t connection,
    dispatch_block_t barrier)
{
    if (!connection || !barrier) return;
    /* mach_msg sends are already ordered and synchronous per peer, so the
     * barrier has nothing left to serialize; run it inline. */
    barrier();
}

#pragma mark - Async reply waiter

typedef struct async_reply_ctx {
    struct _xpc_connection_s *conn;     /* retained */
    xpc_object_t message;               /* retained */
    xpc_handler_t handler;              /* copied */
    mach_port_t reply_port;
} async_reply_ctx_t;

static void
async_reply_add_port(struct _xpc_connection_s *c, mach_port_t port)
{
    if (c->reply_count == c->reply_cap) {
        size_t newcap = c->reply_cap ? c->reply_cap * 2 : 4;
        mach_port_t *np = realloc(c->reply_ports, newcap * sizeof(*np));
        if (!np) return;
        c->reply_ports = np;
        c->reply_cap = newcap;
    }
    c->reply_ports[c->reply_count++] = port;
}

static void
async_reply_remove_port(struct _xpc_connection_s *c, mach_port_t port)
{
    for (size_t i = 0; i < c->reply_count; i++) {
        if (c->reply_ports[i] == port) {
            c->reply_ports[i] = c->reply_ports[c->reply_count - 1];
            c->reply_count--;
            return;
        }
    }
}

static void *
async_reply_main(void *arg)
{
    async_reply_ctx_t *ctx = arg;
    struct _xpc_connection_s *c = ctx->conn;

    int sres = conn_send(c, ctx->message, ctx->reply_port);
    if (sres != KERN_SUCCESS) goto out_error;

    xpc_object_t reply = NULL;
    mach_port_t port = ctx->reply_port;
    int rrc = xpc_pipe_try_receive(&port, &reply, NULL, NULL, 65536, 0);
    if (rrc != 0 || !reply) goto out_error;

    ctx->handler(reply);
    xpc_release(reply);
    goto out_done;

out_error:
    ctx->handler((xpc_object_t)&_xpc_error_connection_interrupted);
out_done:
    pthread_mutex_lock(&c->lock);
    async_reply_remove_port(c, ctx->reply_port);
    pthread_mutex_unlock(&c->lock);
    destroy_receive(&ctx->reply_port);
    Block_release(ctx->handler);
    xpc_release(ctx->message);
    xpc_release((xpc_object_t)c);
    free(ctx);
    return NULL;
}

void
xpc_connection_send_message_with_reply(xpc_connection_t connection,
    xpc_object_t message, dispatch_queue_t replyq, xpc_handler_t handler)
{
    (void)replyq;
    if (!connection || !message || !handler) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;

    conn_resolve_peer(c);

    mach_port_t reply_port = MACH_PORT_NULL;
    async_reply_ctx_t *ctx = NULL;
    pthread_mutex_lock(&c->lock);
    if (c->cancelled || !MACH_PORT_VALID(c->peer_port)) {
        pthread_mutex_unlock(&c->lock);
        handler((xpc_object_t)&_xpc_error_connection_invalid);
        return;
    }
    mach_port_name_t name;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &name) != KERN_SUCCESS) {
        pthread_mutex_unlock(&c->lock);
        handler((xpc_object_t)&_xpc_error_connection_invalid);
        return;
    }
    reply_port = name;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        destroy_receive(&reply_port);
        pthread_mutex_unlock(&c->lock);
        handler((xpc_object_t)&_xpc_error_connection_invalid);
        return;
    }
    async_reply_add_port(c, reply_port);
    ctx->conn = c;
    ctx->reply_port = reply_port;
    ctx->handler = Block_copy(handler);
    ctx->message = message;
    xpc_retain((xpc_object_t)c);
    xpc_retain(message);
    pthread_t t;
    if (pthread_create(&t, NULL, async_reply_main, ctx) != 0) {
        async_reply_remove_port(c, reply_port);
        destroy_receive(&reply_port);
        Block_release(ctx->handler);
        xpc_release((xpc_object_t)c);
        xpc_release(message);
        free(ctx);
        pthread_mutex_unlock(&c->lock);
        handler((xpc_object_t)&_xpc_error_connection_invalid);
        return;
    }
    pthread_mutex_unlock(&c->lock);
    /* On success ctx is owned by the waiter thread. */
}

xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t connection,
    xpc_object_t message)
{
    if (!connection || !message) return NULL;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;

    conn_resolve_peer(c);

    mach_port_t reply_port = MACH_PORT_NULL;
    pthread_mutex_lock(&c->lock);
    if (c->cancelled || !MACH_PORT_VALID(c->peer_port)) {
        pthread_mutex_unlock(&c->lock);
        return (xpc_object_t)&_xpc_error_connection_invalid;
    }
    mach_port_name_t name;
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &name) != KERN_SUCCESS) {
        pthread_mutex_unlock(&c->lock);
        return (xpc_object_t)&_xpc_error_connection_invalid;
    }
    reply_port = name;
    async_reply_add_port(c, reply_port);
    pthread_mutex_unlock(&c->lock);

    int sres = conn_send(c, message, reply_port);
    xpc_object_t reply = NULL;
    if (sres == KERN_SUCCESS) {
        mach_port_t port = reply_port;
        int rrc = xpc_pipe_try_receive(&port, &reply, NULL, NULL, 65536, 0);
        if (rrc != 0 || !reply) {
            pthread_mutex_lock(&c->lock);
            bool cancelled = c->cancelled;
            pthread_mutex_unlock(&c->lock);
            reply = (xpc_object_t)(cancelled
                ? &_xpc_error_connection_invalid
                : &_xpc_error_connection_interrupted);
        }
    } else {
        pthread_mutex_lock(&c->lock);
        bool cancelled = c->cancelled;
        pthread_mutex_unlock(&c->lock);
        reply = (xpc_object_t)(cancelled
            ? &_xpc_error_connection_invalid
            : &_xpc_error_connection_interrupted);
    }

    pthread_mutex_lock(&c->lock);
    async_reply_remove_port(c, reply_port);
    pthread_mutex_unlock(&c->lock);
    destroy_receive(&reply_port);
    return reply;
}

#pragma mark - Identity

const char *
xpc_connection_get_name(xpc_connection_t connection)
{
    if (!connection) return NULL;
    return ((struct _xpc_connection_s *)(void *)connection)->name;
}

static bool
conn_audit(struct _xpc_connection_s *c, audit_token_t *out)
{
    pthread_mutex_lock(&c->lock);
    bool have = c->have_peer_audit;
    if (have) *out = c->peer_audit;
    pthread_mutex_unlock(&c->lock);
    return have;
}

pid_t
xpc_connection_get_pid(xpc_connection_t connection)
{
    if (!connection) return 0;
    audit_token_t t;
    if (!conn_audit((struct _xpc_connection_s *)(void *)connection, &t))
        return 0;
    return (pid_t)audit_token_to_pid(t);
}

uid_t
xpc_connection_get_euid(xpc_connection_t connection)
{
    if (!connection) return (uid_t)-1;
    audit_token_t t;
    if (!conn_audit((struct _xpc_connection_s *)(void *)connection, &t))
        return (uid_t)-1;
    return (uid_t)audit_token_to_euid(t);
}

gid_t
xpc_connection_get_egid(xpc_connection_t connection)
{
    if (!connection) return (gid_t)-1;
    audit_token_t t;
    if (!conn_audit((struct _xpc_connection_s *)(void *)connection, &t))
        return (gid_t)-1;
    return (gid_t)audit_token_to_egid(t);
}

au_asid_t
xpc_connection_get_asid(xpc_connection_t connection)
{
    if (!connection) return AU_ASSIGN_ASID;
    audit_token_t t;
    if (!conn_audit((struct _xpc_connection_s *)(void *)connection, &t))
        return AU_ASSIGN_ASID;
    return (au_asid_t)audit_token_to_asid(t);
}

#pragma mark - Context / finalizer

void
xpc_connection_set_context(xpc_connection_t connection, void *context)
{
    if (!connection) return;
    ((struct _xpc_connection_s *)(void *)connection)->context = context;
}

void *
xpc_connection_get_context(xpc_connection_t connection)
{
    if (!connection) return NULL;
    return ((struct _xpc_connection_s *)(void *)connection)->context;
}

void
xpc_connection_set_finalizer_f(xpc_connection_t connection,
    xpc_finalizer_t finalizer)
{
    if (!connection) return;
    ((struct _xpc_connection_s *)(void *)connection)->finalizer = finalizer;
}

/*
 * xpc_connection_set_event_channel(connection, flag) — SPI flag setter for
 * event-channel connections.  Apple refuses peer connections
 * ("Can't set event channel on peer connection"); the listener-only guard
 * below mirrors that.  The flag itself is informational here: our event
 * delivery path already reads the struct's handler/queue fields directly.
 */
void
xpc_connection_set_event_channel(xpc_connection_t connection, bool flag)
{
    if (!connection) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    if (c->listener) {
        c->event_channel = flag;
        return;
    }
    fprintf(stderr, "Can't set event channel on peer connection\n");
    abort();
}

#pragma mark - Peer requirements

static int
conn_store_requirement(struct _xpc_connection_s *c,
    xpc_peer_requirement_t req)
{
    if (!req || !XPC_OBJECT_CHECK(req, &_xpc_type_peer_requirement)) {
        xpc_release((xpc_object_t)req);
        return -1;
    }
    pthread_mutex_lock(&c->lock);
    if (c->peer_requirement) {
        xpc_release((xpc_object_t)(void *)c->peer_requirement);
    }
    c->peer_requirement = req;
    c->invalidate_on_requirement_failure = true;
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int
xpc_connection_set_peer_code_signing_requirement(xpc_connection_t connection,
    const char *requirement)
{
    if (!connection || !requirement) return -1;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    free(c->peer_code_signing_requirement);
    c->peer_code_signing_requirement = strdup(requirement);
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_CODE_SIGNING, strdup(requirement));
    if (!r) return -1;
    return conn_store_requirement(c, (xpc_peer_requirement_t)(void *)r);
}

int
xpc_connection_set_peer_entitlement_exists_requirement(xpc_connection_t connection,
    const char *entitlement)
{
    if (!connection) return -1;
    xpc_rich_error_t err = NULL;
    xpc_peer_requirement_t req =
        xpc_peer_requirement_create_entitlement_exists(entitlement, &err);
    if (err) xpc_release((xpc_object_t)err);
    if (!req) return -1;
    return conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, req);
}

int
xpc_connection_set_peer_entitlement_matches_value_requirement(
    xpc_connection_t connection, const char *entitlement, xpc_object_t value)
{
    if (!connection) return -1;
    xpc_rich_error_t err = NULL;
    xpc_peer_requirement_t req =
        xpc_peer_requirement_create_entitlement_matches_value(entitlement,
            value, &err);
    if (err) xpc_release((xpc_object_t)err);
    if (!req) return -1;
    return conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, req);
}

int
xpc_connection_set_peer_team_identity_requirement(xpc_connection_t connection,
    const char *signing_identifier)
{
    if (!connection) return -1;
    xpc_rich_error_t err = NULL;
    xpc_peer_requirement_t req =
        xpc_peer_requirement_create_team_identity(signing_identifier, &err);
    if (err) xpc_release((xpc_object_t)err);
    if (!req) return -1;
    return conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, req);
}

int
xpc_connection_set_peer_platform_identity_requirement(xpc_connection_t connection,
    const char *signing_identifier)
{
    if (!connection) return -1;
    xpc_rich_error_t err = NULL;
    xpc_peer_requirement_t req =
        xpc_peer_requirement_create_platform_identity(signing_identifier,
            &err);
    if (err) xpc_release((xpc_object_t)err);
    if (!req) return -1;
    return conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, req);
}

int
xpc_connection_set_peer_lightweight_code_requirement(xpc_connection_t connection,
    xpc_object_t lwcr)
{
    if (!connection) return -1;
    xpc_rich_error_t err = NULL;
    xpc_peer_requirement_t req = xpc_peer_requirement_create_lwcr(lwcr, &err);
    if (err) xpc_release((xpc_object_t)err);
    if (!req) return -1;
    return conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, req);
}

void
xpc_connection_set_peer_requirement(xpc_connection_t connection,
    xpc_peer_requirement_t peer_requirement)
{
    if (!connection) return;
    conn_store_requirement(
        (struct _xpc_connection_s *)(void *)connection, peer_requirement);
}

char *
xpc_connection_copy_invalidation_reason(xpc_connection_t connection)
{
    if (!connection) return NULL;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    pthread_mutex_lock(&c->lock);
    char *r = c->invalidation_reason ? strdup(c->invalidation_reason) : NULL;
    pthread_mutex_unlock(&c->lock);
    return r;
}

#pragma mark - Dispose

void
xpc_connection_dispose(xpc_connection_t connection)
{
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;

    pthread_mutex_lock(&c->lock);
    c->shutdown = true;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);

    if (c->rx_thread_started) {
        pthread_join(c->rx_thread, NULL);
    }

    if (c->finalizer) c->finalizer(c->context);
    if (c->handler) Block_release(c->handler);
    free(c->name);
    free(c->invalidation_reason);
    free(c->peer_code_signing_requirement);
    if (c->peer_requirement) {
        xpc_release((xpc_object_t)(void *)c->peer_requirement);
    }
    for (size_t i = 0; i < c->pending_count; i++) {
        xpc_release(c->pending[i]);
    }
    free(c->pending);
    free(c->reply_ports);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->lock);
}