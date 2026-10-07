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
 * xpc_listener.c — the listener type (xpc/listener.h).
 *
 * A listener is the service-side counterpart of the session: it owns a named
 * listening connection and mints one incoming peer session per client
 * connection.  In this hermetic transport the listener registers its name at
 * activate() so clients resolve it with xpc_connection_create_mach_service();
 * the first message on the shared receive port mints the peer, later messages
 * route to the peer's incoming-message handler, and error events terminate
 * the peer with a rich error to its cancel handler.
 *
 * Lifetime model: the listener stores itself as the backing connection's
 * *raw* context pointer (never retained — a retained context would cycle and
 * xpc_connection_set_context() does not run a finalizer).  The delivery
 * adapter captures only the connection and fetches the listener through
 * xpc_connection_get_context() at each invocation.  xpc_listener_dispose()
 * nulls the context, cancels the backing connection (destroying its receive
 * right so the receive thread unblocks), then releases it; the release joins
 * the receive thread, so no adapter invocation can outlive the listener
 * struct.  The peer session's ->owner back pointer is likewise non-retained;
 * the listener owns the peer in its single ->peer slot and clears ->owner
 * before dropping it.
 *
 * Concurrency: calls are assumed serialized as in Apple (handler runs on the
 * backing connection's receive thread; xpc_listener_reject_peer() only from
 * inside the incoming-session handler; cancel/dispose when quiesced).
 */

#include "xpc_internal.h"

#include <Block.h>

#pragma mark - Helpers

static xpc_rich_error_t
listener_rich_error(const char *desc, bool can_retry)
{
    return xpc_rich_error_create(desc, can_retry);
}

static const char *
listener_error_description(xpc_object_t event)
{
    if (event == (xpc_object_t)&_xpc_error_connection_interrupted) {
        return "Connection interrupted";
    }
    if (event == (xpc_object_t)&_xpc_error_connection_invalid) {
        return "Connection invalid";
    }
    if (event == (xpc_object_t)&_xpc_error_termination_imminent) {
        return "Service should not have launched";
    }
    if (event == (xpc_object_t)&_xpc_error_peer_code_signing_requirement) {
        return "Peer does not satisfy code signing requirement";
    }
    return "Connection failure";
}

/* Deliver a rich error to the peer's cancel handler, if one is set. */
static void
listener_notify_peer_error(struct _xpc_session_s *s, xpc_object_t event)
{
    if (!s || !s->cancel_handler) return;
    xpc_rich_error_t rich = listener_rich_error(
        listener_error_description(event), false);
    if (rich) {
        s->cancel_handler(rich);
        xpc_release((xpc_object_t)rich);
    }
}

#pragma mark - Delivery

/* Dispatch an event from the backing connection:
 *
 *   - error events terminate the current peer (rich error to its cancel
 *     handler) and clear the peer slot;
 *   - the first dictionary from a client mints an incoming peer session and
 *     hands it to the listener's incoming-session handler; the triggering
 *     message then flows to the peer's message handler unless the handler
 *     rejected or cancelled the peer;
 *   - subsequent dictionaries route to the peer's message handler.
 */
static void
listener_deliver(struct _xpc_listener_s *l, xpc_object_t event)
{
    if (!event) return;

    if (xpc_get_type(event) == &_xpc_type_error) {
        struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)l->peer;
        if (s) {
            l->peer = NULL;
            s->owner = NULL;
            s->cancelled = true;
            listener_notify_peer_error(s, event);
            xpc_release((xpc_object_t)s);
        }
        return;
    }

    if (l->cancelled) return;
    if (xpc_get_type(event) != &_xpc_type_dictionary) return;

    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)l->peer;
    if (!s) {
        /* First message from a client: mint the incoming peer session.
         * Unless the handler rejects it, the listener owns it in ->peer. */
        s = (struct _xpc_session_s *)(void *)
            xpc_object_alloc(&_xpc_type_session, sizeof(*s));
        if (!s) return;
        s->connection = NULL;
        s->activated = true;
        s->owner = l;
        l->peer = (xpc_session_t)(void *)s;
        if (l->incoming_session_handler) {
            l->incoming_session_handler((xpc_session_t)(void *)s);
        }
        s = (struct _xpc_session_s *)(void *)l->peer;
        if (!s) return;         /* rejected: slot cleared, ownership released */
        if (s->cancelled) return;
    }
    if (s->incoming_handler) s->incoming_handler(event);
}

/* Install the delivery adapter as the backing connection's event handler.
 * The adapter captures only the connection pointer (no retain on the
 * listener); it reaches the listener through the connection context at each
 * invocation so dispose can cut delivery by nulling the context. */
static void
listener_install_adapter(struct _xpc_listener_s *l)
{
    xpc_connection_t conn = l->connection;
    xpc_handler_t adapter = ^(xpc_object_t event) {
        struct _xpc_listener_s *listener =
            (struct _xpc_listener_s *)xpc_connection_get_context(conn);
        if (listener) listener_deliver(listener, event);
    };
    xpc_connection_set_event_handler(conn, adapter);
    Block_release(adapter);
    xpc_connection_set_context(conn, l);
}

#pragma mark - Construction

xpc_listener_t
xpc_listener_create(const char *service, dispatch_queue_t target_queue,
    xpc_listener_create_flags_t flags,
    xpc_listener_incoming_session_handler_t incoming_session_handler,
    xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!service || !*service) {
        if (error_out) {
            *error_out = listener_rich_error("Service name required", true);
        }
        return NULL;
    }
    if (!incoming_session_handler) {
        if (error_out) {
            *error_out = listener_rich_error(
                "Incoming session handler required", true);
        }
        return NULL;
    }
    xpc_connection_t conn = xpc_connection_create(service, target_queue);
    if (!conn) {
        if (error_out) {
            *error_out = listener_rich_error(
                "Unable to create listener connection", true);
        }
        return NULL;
    }
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)
        xpc_object_alloc(&_xpc_type_listener, sizeof(*l));
    if (!l) {
        xpc_release((xpc_object_t)(void *)conn);
        if (error_out) {
            *error_out = listener_rich_error("Out of memory", true);
        }
        return NULL;
    }
    l->connection = conn;
    l->name = strdup(service);
    l->incoming_session_handler = Block_copy(incoming_session_handler);
    listener_install_adapter(l);

    /* Unless XPC_LISTENER_CREATE_INACTIVE, the listener starts accepting
     * immediately (the backing connection registers the name). */
    if (!(flags & XPC_LISTENER_CREATE_INACTIVE)) {
        l->activated = true;
        xpc_connection_activate(conn);
    }
    return (xpc_listener_t)(void *)l;
}

xpc_listener_t
xpc_listener_create_anonymous(void)
{
    /* An anonymous listener has no registry name: clients reach it only
     * through an endpoint from xpc_listener_create_endpoint().  It is
     * active immediately (its port is already wrapped by the endpoint). */
    xpc_connection_t conn = xpc_connection_create(NULL, NULL);
    if (!conn) return NULL;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)
        xpc_object_alloc(&_xpc_type_listener, sizeof(*l));
    if (!l) {
        xpc_release((xpc_object_t)(void *)conn);
        return NULL;
    }
    l->connection = conn;
    l->name = NULL;
    listener_install_adapter(l);
    l->activated = true;
    xpc_connection_activate(conn);
    return (xpc_listener_t)(void *)l;
}

#pragma mark - Lifecycle

bool
xpc_listener_activate(xpc_listener_t listener, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) {
        if (error_out) {
            *error_out = listener_rich_error("Invalid listener", false);
        }
        return false;
    }
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (l->cancelled) {
        if (error_out) {
            *error_out = listener_rich_error("Listener is cancelled", false);
        }
        return false;
    }
    if (l->activated) return true;
    if (!l->connection) {
        if (error_out) {
            *error_out = listener_rich_error(
                "Listener has no backing connection", false);
        }
        return false;
    }
    l->activated = true;
    xpc_connection_activate(l->connection);
    return true;
}

void
xpc_listener_cancel(xpc_listener_t listener)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (l->cancelled) return;
    l->cancelled = true;
    /* Cancelling the backing connection invalidates it, which synchronously
     * delivers a connection-invalid error to the adapter and thereby
     * terminates the current peer (rich error to its cancel handler). */
    if (l->connection) xpc_connection_cancel(l->connection);
}

void
xpc_listener_dispose(xpc_listener_t listener)
{
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (l->connection) {
        /* Cut delivery first: after this, no adapter invocation touches the
         * listener, so the teardown below races with nothing. */
        xpc_connection_set_context(l->connection, NULL);
        xpc_connection_cancel(l->connection);
        /* The release joins the receive thread (if it ever started), which
         * waits out any invocation already in flight; the connection is
         * otherwise owned only by this listener, so the listener struct may
         * be freed immediately afterwards. */
        xpc_release((xpc_object_t)l->connection);
    }
    if (l->peer) {
        struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)l->peer;
        s->owner = NULL;
        s->cancelled = true;
        xpc_release((xpc_object_t)l->peer);
    }
    if (l->incoming_session_handler) Block_release(l->incoming_session_handler);
    free(l->name);
}

#pragma mark - Peer rejection

void
xpc_listener_reject_peer(xpc_session_t peer, const char *reason)
{
    if (!XPC_OBJECT_CHECK(peer, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)peer;
    struct _xpc_listener_s *l = s->owner;
    if (!l || l->peer != peer) return;   /* not this listener's current peer */
    l->peer = NULL;
    s->owner = NULL;
    s->cancelled = true;
    if (s->cancel_handler) {
        xpc_rich_error_t rich = listener_rich_error(
            reason && *reason ? reason : "Session rejected", false);
        if (rich) {
            s->cancel_handler(rich);    /* runs before the listener's ref drops */
            xpc_release((xpc_object_t)rich);
        }
    }
    /* The listener's ownership of the peer ends here; the caller's handler
     * must not use the peer afterwards. */
    xpc_release((xpc_object_t)s);
}

#pragma mark - Description

char *
xpc_listener_copy_description(xpc_listener_t listener)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return NULL;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    char *desc = NULL;
    if (l->name && asprintf(&desc, "<listener for service '%s'>", l->name) > 0) {
        return desc;
    }
    return strdup("<listener>");
}

#pragma mark - Endpoints

xpc_endpoint_t
xpc_listener_create_endpoint(xpc_listener_t listener)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return NULL;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (!l->connection || l->cancelled) return NULL;
    /* An endpoint implies an accepting listener: activate if the listener
     * was created inactive so the wrapped port actually receives. */
    if (!l->activated) {
        l->activated = true;
        xpc_connection_activate(l->connection);
    }
    return (xpc_endpoint_t)xpc_endpoint_create(xpc_connection_get_port(
        (xpc_object_t)l->connection));
}

#pragma mark - Handler and peer requirement configuration

void
xpc_listener_set_incoming_session_handler(xpc_listener_t listener,
    xpc_listener_incoming_session_handler_t handler)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    xpc_listener_incoming_session_handler_t old =
        l->incoming_session_handler;
    l->incoming_session_handler = handler ? Block_copy(handler) : NULL;
    if (old) Block_release(old);
}

int
xpc_listener_set_peer_code_signing_requirement(xpc_listener_t listener,
    const char *requirement)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return -1;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (!l->connection) return -1;
    return xpc_connection_set_peer_code_signing_requirement(
        l->connection, requirement);
}

void
xpc_listener_set_peer_requirement(xpc_listener_t listener,
    xpc_peer_requirement_t requirement)
{
    if (!XPC_OBJECT_CHECK(listener, &_xpc_type_listener)) return;
    struct _xpc_listener_s *l = (struct _xpc_listener_s *)(void *)listener;
    if (l->connection) {
        xpc_connection_set_peer_requirement(l->connection,
            requirement);
    }
}