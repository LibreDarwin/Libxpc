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
 * xpc_set_event_stream_handler(): subscribe to a named event stream.
 *
 * Apple's implementation builds a mach-service LISTENER connection named
 * after the stream, marks it as an event channel, stuffs
 * { connection, targetq, __Block_copy'd handler } into the connection
 * context, drives delivery off a connection event-handler callback
 * (__xpc_events_listener_event) and a __xpc_events_ctx_finalizer, then
 * parks the retained connection in a process-global __event_listeners
 * array under a lock.  A NULL target queue defaults to the main queue.
 *
 * This port mirrors that shape through the public connection API: the
 * listener is created with XPC_CONNECTION_MACH_SERVICE_LISTENER, the
 * context holds { connection, queue, handler }, and the connection's event
 * handler re-dispatches every inbound dictionary onto the target queue
 * before the caller's handler runs.  Named services are in-process here
 * (a g_registry name table, not the session bootstrap), so a publisher in
 * the same process can reach the stream, and a second subscription to the
 * same stream replaces the first rather than broadcasting to both.  The
 * queue is deliberately not retained: system dispatch_retain/dispatch_release
 * are deprecated and the dylib links the host libSystem, so subscribers
 * keep their queue alive for the subscription lifetime, which is Apple's
 * documented model anyway.
 */

#include "xpc_internal.h"

#include <Block.h>
#include <pthread.h>

struct xpc_event_ctx_s {
    xpc_connection_t connection;
    dispatch_queue_t queue;
    xpc_event_handler_t handler;
};

static pthread_mutex_t __event_listeners_lock = PTHREAD_MUTEX_INITIALIZER;
static xpc_object_t __event_listeners = NULL;

static void
xpc_event_ctx_finalizer(void *context)
{
    struct xpc_event_ctx_s *ctx = context;
    if (!ctx) return;
    if (ctx->connection) {
        xpc_release((xpc_object_t)ctx->connection);
    }
    if (ctx->handler) {
        Block_release(ctx->handler);
    }
    free(ctx);
}

void
xpc_set_event_stream_handler(const char *stream, dispatch_queue_t targetq,
    xpc_event_handler_t handler)
{
    if (!stream || !handler) return;
    if (!targetq) targetq = dispatch_get_main_queue();

    struct xpc_event_ctx_s *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return;

    xpc_connection_t conn = xpc_connection_create_mach_service(stream,
        targetq, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!conn) {
        free(ctx);
        return;
    }

    ctx->connection = conn;
    xpc_retain((xpc_object_t)conn);	/* balanced in the finalizer */
    ctx->queue = targetq;
    ctx->handler = Block_copy(handler);

    xpc_connection_set_context(conn, ctx);
    xpc_connection_set_finalizer_f(conn, xpc_event_ctx_finalizer);
    xpc_connection_set_event_handler(conn, ^(xpc_object_t event) {
        struct xpc_event_ctx_s *c = xpc_connection_get_context(conn);
        if (!c || !c->handler) return;
        if (xpc_get_type(event) != &_xpc_type_dictionary) return;
        xpc_retain(event);
        dispatch_async(c->queue, ^{
            c->handler(event);
            xpc_release(event);
        });
    });
    xpc_connection_activate(conn);

    pthread_mutex_lock(&__event_listeners_lock);
    if (!__event_listeners) {
        __event_listeners = xpc_array_create(NULL, 0);
    }
    if (__event_listeners) {
        xpc_array_append_value(__event_listeners, (xpc_object_t)conn);
    }
    pthread_mutex_unlock(&__event_listeners_lock);

    xpc_release((xpc_object_t)conn);
}