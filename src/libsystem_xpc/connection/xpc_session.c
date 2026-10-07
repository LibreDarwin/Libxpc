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
 * xpc_session.c — the session type (xpc/session.h).
 *
 * A session is a manageability wrapper over a client connection: it
 * presents the service-facing trio of an incoming-message handler, a
 * cancel handler that receives a rich error, and send/reply helpers that
 * report failures through xpc_rich_error_t instead of the connection's
 * event handler.  The session owns its backing connection (retained) and
 * installs an adapter as the connection's event handler at activate().
 *
 * Handler model: the session copies the message and cancel handlers when
 * they are set; activate() wraps them in a small adapter block that maps
 * connection error objects (XPC_TYPE_ERROR) onto the cancel handler's
 * rich error, and routes anything else to the incoming-message handler.
 * The adapter captures the handler blocks (retained by the runtime on
 * copy), never the session, so it stays valid for the lifetime of the
 * backing connection.
 */

#include "xpc_internal.h"

#include <Block.h>

#pragma mark - Helpers

static xpc_rich_error_t
session_rich_error(const char *desc, bool can_retry)
{
    return xpc_rich_error_create(desc, can_retry);
}

static const char *
session_error_description(xpc_object_t event)
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

static xpc_object_t
session_message_from_reply(xpc_object_t reply)
{
    /* Connection reply paths deliver either a real reply object or one of
     * the XPC_ERROR_* sentinels.  Distinguish by object type. */
    if (reply && xpc_get_type(reply) == &_xpc_type_error) return NULL;
    return reply;
}

/* The adapter installed as the backing connection's event handler.
 * Error-toned events reach the session cancel handler as a rich error;
 * everything else reaches the incoming-message handler. */
static xpc_handler_t
session_make_adapter(xpc_session_incoming_message_handler_t incoming,
    xpc_session_cancel_handler_t cancel)
{
    if (!incoming && !cancel) return NULL;
    xpc_handler_t adapter = ^(xpc_object_t event) {
        if (event && xpc_get_type(event) == &_xpc_type_error) {
            if (cancel) {
                xpc_rich_error_t rich =
                    session_rich_error(session_error_description(event), false);
                cancel(rich);
                xpc_release((xpc_object_t)rich);
            }
            return;
        }
        if (incoming) incoming(event);
    };
    return Block_copy(adapter);
}

#pragma mark - Construction

xpc_session_t
xpc_session_create_xpc_service(const char *name, dispatch_queue_t target_queue,
    xpc_session_create_flags_t flags, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!name || !*name) {
        if (error_out) {
            *error_out = session_rich_error("Service name required", true);
        }
        return NULL;
    }
    xpc_connection_t conn = xpc_connection_create_mach_service(name,
        target_queue, 0);
    if (!conn) {
        if (error_out) {
            *error_out = session_rich_error("Unable to create connection to service", true);
        }
        return NULL;
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)
        xpc_object_alloc(&_xpc_type_session, sizeof(*s));
    if (!s) {
        xpc_release((xpc_object_t)(void *)conn);
        if (error_out) {
            *error_out = session_rich_error("Out of memory", true);
        }
        return NULL;
    }
    s->connection = conn;
    (void)flags;
    return (xpc_session_t)(void *)s;
}

xpc_session_t
xpc_session_create_mach_service(const char *mach_service,
    dispatch_queue_t target_queue, xpc_session_create_flags_t flags,
    xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!mach_service || !*mach_service) {
        if (error_out) {
            *error_out = session_rich_error("Service name required", true);
        }
        return NULL;
    }
    uint64_t cflags = 0;
    if (flags & XPC_SESSION_CREATE_MACH_PRIVILEGED) {
        cflags |= XPC_CONNECTION_MACH_SERVICE_PRIVILEGED;
    }
    xpc_connection_t conn = xpc_connection_create_mach_service(mach_service,
        target_queue, cflags);
    if (!conn) {
        if (error_out) {
            *error_out = session_rich_error("Unable to create connection to service", true);
        }
        return NULL;
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)
        xpc_object_alloc(&_xpc_type_session, sizeof(*s));
    if (!s) {
        xpc_release((xpc_object_t)(void *)conn);
        if (error_out) {
            *error_out = session_rich_error("Out of memory", true);
        }
        return NULL;
    }
    s->connection = conn;
    (void)flags;
    return (xpc_session_t)(void *)s;
}

#pragma mark - Handler and queue configuration

void
xpc_session_set_incoming_message_handler(xpc_session_t session,
    xpc_session_incoming_message_handler_t handler)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    xpc_session_incoming_message_handler_t old = s->incoming_handler;
    s->incoming_handler = handler ? Block_copy(handler) : NULL;
    if (old) Block_release(old);
}

void
xpc_session_set_cancel_handler(xpc_session_t session,
    xpc_session_cancel_handler_t cancel_handler)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) {
        return;
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    xpc_session_cancel_handler_t old = s->cancel_handler;
    s->cancel_handler = cancel_handler ? Block_copy(cancel_handler) : NULL;
    if (old) Block_release(old);
}

void
xpc_session_set_target_queue(xpc_session_t session, dispatch_queue_t target_queue)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->connection) {
        xpc_connection_set_target_queue(s->connection, target_queue);
    }
}

#pragma mark - Lifecycle

bool
xpc_session_activate(xpc_session_t session, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session) || session == NULL) {
        if (error_out) {
            *error_out = session_rich_error("Invalid session", false);
        }
        return false;
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->activated) return true;
    if (!s->connection) {
        if (error_out) {
            *error_out = session_rich_error("Session has no backing connection", false);
        }
        return false;
    }
    xpc_handler_t adapter = session_make_adapter(s->incoming_handler,
        s->cancel_handler);
    if (adapter) {
        xpc_connection_set_event_handler(s->connection, adapter);
        Block_release(adapter);
    }
    s->activated = true;
    xpc_connection_activate(s->connection);
    return true;
}

void
xpc_session_cancel(xpc_session_t session)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    s->cancelled = true;
    if (s->connection) xpc_connection_cancel(s->connection);
}

#pragma mark - Description

char *
xpc_session_copy_description(xpc_session_t session)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return NULL;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->connection) {
        const char *svc = xpc_connection_get_name(s->connection);
        char *desc = NULL;
        if (svc && asprintf(&desc, "<session for service '%s'>", svc) > 0) {
            return desc;
        }
    }
    return strdup("<session>");
}

#pragma mark - Messaging

xpc_rich_error_t
xpc_session_send_message(xpc_session_t session, xpc_object_t message)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) {
        return session_rich_error("Invalid session", false);
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->cancelled || !s->connection) {
        return session_rich_error("Session is cancelled", false);
    }
    xpc_connection_send_message(s->connection, message);
    return NULL;
}

xpc_object_t
xpc_session_send_message_with_reply_sync(xpc_session_t session,
    xpc_object_t message, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) {
        if (error_out) {
            *error_out = session_rich_error("Invalid session", false);
        }
        return NULL;
    }
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->cancelled || !s->connection) {
        if (error_out) {
            *error_out = session_rich_error("Session is cancelled", false);
        }
        return NULL;
    }
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(
        s->connection, message);
    xpc_object_t ok = session_message_from_reply(reply);
    if (!ok) {
        if (error_out) {
            *error_out = session_rich_error(
                session_error_description(reply), false);
        }
        return NULL;
    }
    return ok;
}

void
xpc_session_send_message_with_reply_async(xpc_session_t session,
    xpc_object_t message, xpc_session_reply_handler_t reply_handler)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->cancelled || !s->connection || !reply_handler) return;

    /* Wrap the session reply handler so connection error objects become a
     * NULL reply plus a rich error, matching the session contract. */
    xpc_handler_t adapter = ^(xpc_object_t reply) {
        xpc_object_t ok = session_message_from_reply(reply);
        if (ok) {
            reply_handler(ok, NULL);
        } else {
            xpc_rich_error_t rich =
                session_rich_error(session_error_description(reply), false);
            reply_handler(NULL, rich);
            xpc_release((xpc_object_t)rich);
        }
    };
    xpc_connection_send_message_with_reply(s->connection, message, NULL,
        adapter);
    Block_release(adapter);
}

#pragma mark - Peer requirements

int
xpc_session_set_peer_code_signing_requirement(xpc_session_t session,
    const char *requirement)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return -1;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (!s->connection) return -1;
    return xpc_connection_set_peer_code_signing_requirement(
        s->connection, requirement);
}

void
xpc_session_set_peer_requirement(xpc_session_t session,
    xpc_peer_requirement_t requirement)
{
    if (!XPC_OBJECT_CHECK(session, &_xpc_type_session)) return;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)session;
    if (s->connection) {
        xpc_connection_set_peer_requirement(s->connection, requirement);
    }
}