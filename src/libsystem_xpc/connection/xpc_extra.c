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
 * xpc_extra.c — convenience APIs for the endpoint, activity, and listener
 * types, the typed dictionary accessors declared in xpc.h, and the reply
 * context machinery.
 *
 * The connection and session types live in xpc_connection.c and
 * xpc_session.c; only endpoint-shaped helpers that those files build on
 * remain here.
 */

#include "xpc_internal.h"

#pragma mark - Endpoint

xpc_object_t
xpc_endpoint_create(mach_port_t port)
{
    struct _xpc_endpoint_s *e = (struct _xpc_endpoint_s *)(void *)
        xpc_object_alloc(&_xpc_type_endpoint, sizeof(*e));
    if (e) e->port = port;
    return (xpc_object_t)e;
}

mach_port_t
xpc_endpoint_get_port(xpc_object_t endpoint)
{
    if (!XPC_OBJECT_CHECK(endpoint, &_xpc_type_endpoint))
        return MACH_PORT_NULL;
    return ((struct _xpc_endpoint_s *)(void *)endpoint)->port;
}

xpc_object_t
xpc_endpoint_copy_listener_port(xpc_object_t endpoint)
{
    (void)endpoint;
    return NULL;
}

#pragma mark - Connection convenience built on the endpoint type

void
xpc_dictionary_set_connection(xpc_object_t dict, const char *key,
    xpc_object_t connection)
{
    /* Stores an endpoint holding the connection's advertised port; the
     * dictionary does not retain the connection itself (Apple
     * xpc_dictionary_set_connection(3)). */
    if (!XPC_OBJECT_CHECK(connection, &_xpc_type_connection)) return;
    struct _xpc_connection_s *c = (struct _xpc_connection_s *)(void *)connection;
    xpc_object_t ep = xpc_endpoint_create(c->self_port);
    if (!ep) return;
    xpc_dictionary_set_value(dict, key, ep);
    xpc_release(ep);
}

xpc_object_t
xpc_dictionary_create_connection(xpc_object_t dict, const char *key)
{
    xpc_object_t v = xpc_dictionary_get_value(dict, key);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_endpoint)) return NULL;
    xpc_connection_t conn =
        xpc_connection_create_from_endpoint((xpc_endpoint_t)(void *)v);
    return (xpc_object_t)(void *)conn;
}

#pragma mark - Activity (out of Slice-C scope: no-op shells)

xpc_object_t
xpc_activity_create(xpc_object_t connection)
{
    (void)connection;
    return xpc_null_create();
}

xpc_object_t
xpc_activity_create_from_endpoint(xpc_object_t endpoint)
{
    (void)endpoint;
    return xpc_null_create();
}

void
xpc_activity_resume(xpc_object_t activity) { (void)activity; }
void
xpc_activity_suspend(xpc_object_t activity) { (void)activity; }
void
xpc_activity_cancel(xpc_object_t activity) { (void)activity; }

#pragma mark - Typed dictionary accessors (declared in xpc.h)

void
xpc_dictionary_set_double(xpc_object_t object, const char *key,
    double value)
{
    xpc_object_t v = xpc_double_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_string(xpc_object_t object, const char *key,
    const char *value)
{
    xpc_object_t v = xpc_string_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_data(xpc_object_t object, const char *key,
    const void *bytes, size_t length)
{
    xpc_object_t v = xpc_data_create(bytes, length);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_uuid(xpc_object_t object, const char *key,
    const uuid_t uuid)
{
    xpc_object_t v = xpc_uuid_create(uuid);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_date(xpc_object_t object, const char *key,
    int64_t value)
{
    xpc_object_t v = xpc_date_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

bool
xpc_dictionary_get_bool(xpc_object_t object, const char *key)
{
    return xpc_bool_get_value(xpc_dictionary_get_value(object, key));
}

int64_t
xpc_dictionary_get_int64(xpc_object_t object, const char *key)
{
    return xpc_int64_get_value(xpc_dictionary_get_value(object, key));
}

uint64_t
xpc_dictionary_get_uint64(xpc_object_t object, const char *key)
{
    return xpc_uint64_get_value(xpc_dictionary_get_value(object, key));
}

double
xpc_dictionary_get_double(xpc_object_t object, const char *key)
{
    return xpc_double_get_value(xpc_dictionary_get_value(object, key));
}

const char *
xpc_dictionary_get_string(xpc_object_t object, const char *key)
{
    return xpc_string_get_string_ptr(xpc_dictionary_get_value(object, key));
}

const void *
xpc_dictionary_get_data(xpc_object_t object, const char *key, size_t *length)
{
    xpc_object_t v = xpc_dictionary_get_value(object, key);
    if (length) *length = xpc_data_get_length(v);
    return xpc_data_get_bytes_ptr(v);
}

bool
xpc_dictionary_get_data_np(xpc_object_t object, const char *key,
    const void **bytes, size_t *length)
{
    xpc_object_t v = xpc_dictionary_get_value(object, key);
    if (!XPC_OBJECT_CHECK(v, &_xpc_type_data)) return false;
    if (bytes) *bytes = xpc_data_get_bytes_ptr(v);
    if (length) *length = xpc_data_get_length(v);
    return true;
}

const uint8_t *
xpc_dictionary_get_uuid(xpc_object_t object, const char *key)
{
    return xpc_uuid_get_bytes(xpc_dictionary_get_value(object, key));
}

xpc_object_t
xpc_dictionary_get_date(xpc_object_t object, const char *key)
{
    return xpc_dictionary_get_value(object, key);
}

void
xpc_dictionary_remove_value(xpc_object_t object, const char *key)
{
    xpc_dictionary_set_value(object, key, NULL);
}

#pragma mark - Reply context (xpc_dictionary_create_reply / send_reply)

/*
 * Reply capability: a dictionary that deserialized a received request
 * carries the message's reply right (and the disposition that describes
 * it).  xpc_dictionary_create_reply() mints a fresh dictionary in the
 * "reply" state that owns that capability; sending consumes it.  Modeled
 * on Apple's mach-reply machinery (msgh_local_port arrive → reply goes
 * back on the same port), but the reply context is envelope metadata, not
 * dict content, so it is never serialized.
 */

void
xpc_dictionary_attach_reply_context(xpc_object_t dict, mach_port_t reply_port,
    uint8_t reply_disposition)
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary)) return;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, dict);
    d->msg_mode = 1;
    d->reply_port = reply_port;
    d->reply_disposition = xpc_reply_move_disposition(reply_disposition);
}

/* The received message's right kind → the disposition that moves it back:
 * send-once rights must go out as MOVE_SEND_ONCE, send rights as
 * MOVE_SEND. */
uint8_t
xpc_reply_move_disposition(uint8_t local_bit)
{
    switch (local_bit & 0x1f) {
    case MACH_MSG_TYPE_MOVE_SEND_ONCE:
    case MACH_MSG_TYPE_MAKE_SEND_ONCE:
        return MACH_MSG_TYPE_MOVE_SEND_ONCE;
    default:
        return MACH_MSG_TYPE_MOVE_SEND;
    }
}

xpc_object_t
xpc_dictionary_create_reply(xpc_object_t original)
{
    /* Apple mints a reply only from a dictionary that arrived carrying a
     * reply context -- a message received with a reply port attached -- and
     * consumes that context so the call succeeds at most once. */
    if (!XPC_OBJECT_CHECK(original, &_xpc_type_dictionary)) return NULL;
    xpc_dictionary_t *o = XPC_CAST(xpc_dictionary_t, original);
    if (o->msg_mode != 1 || !MACH_PORT_VALID(o->reply_port)) return NULL;

    xpc_object_t rp = xpc_dictionary_create(NULL, NULL, 0);
    if (!rp) return NULL;
    xpc_dictionary_t *r = XPC_CAST(xpc_dictionary_t, rp);
    r->msg_mode = 2;
    r->reply_port = o->reply_port;
    r->reply_disposition = o->reply_disposition;
    /* Consumed: only one reply may be minted from *original. */
    o->msg_mode = 0;
    o->reply_port = MACH_PORT_NULL;
    return rp;
}

bool
xpc_dictionary_expects_reply(xpc_object_t xdict)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return false;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
    return d->msg_mode != 0 && MACH_PORT_VALID(d->reply_port);
}

void
xpc_dictionary_send_reply(xpc_object_t reply)
{
    /* Apple's __xpc_dictionary_send_reply is destructive and crashy on
     * misuse; ours is a soft no-op when the dictionary does not carry a
     * usable reply context. */
    (void)xpc_reply_send(reply);
}

xpc_object_t
xpc_dictionary_handoff_reply(xpc_object_t reply)
{
    if (!XPC_OBJECT_CHECK(reply, &_xpc_type_dictionary)) return NULL;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, reply);
    if (d->msg_mode != 2 || !MACH_PORT_VALID(d->reply_port)) return NULL;

    xpc_object_t h = xpc_dictionary_create(NULL, NULL, 0);
    if (!h) return NULL;
    xpc_dictionary_t *hh = XPC_CAST(xpc_dictionary_t, h);
    hh->msg_mode = 2;
    hh->reply_port = d->reply_port;
    hh->reply_disposition = d->reply_disposition;
    /* The capability moves; the source keeps none to hand again. */
    d->msg_mode = 0;
    d->reply_port = MACH_PORT_NULL;
    return h;
}

xpc_object_t
xpc_dictionary_handoff_reply_f(xpc_object_t reply,
    void (*finalizer)(void *context), void *context)
{
    xpc_object_t h = xpc_dictionary_handoff_reply(reply);
    if (!h) return NULL;
    xpc_dictionary_t *hh = XPC_CAST(xpc_dictionary_t, h);
    hh->reply_finalizer = finalizer;
    hh->reply_finalizer_ctx = context;
    return h;
}

xpc_object_t
xpc_dictionary_get_remote_connection(xpc_object_t xdict)
{
    /* Apple returns the connection a dictionary arrived on, which only a
     * message received by a connection event handler -- or one minted by
     * xpc_dictionary_create_reply() -- has.  See the note there: none of
     * our dictionaries have one. */
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return NULL;
    return NULL;
}

char *
xpc_copy_description(xpc_object_t object)
{
    return xpc_description_create(object);
}