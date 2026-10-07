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
 * xpc_dictionary_spi.c — dictionary SPI surface (slice 4).
 *
 * Apple's non-public xpc_dictionary_* accessors, decoded from the host
 * libxpc (see disassemble notes in the project log).  The Apple dylib
 * model carries a "received message" envelope on mode-1 dictionaries
 * (request) and mode-2 dictionaries (reply); this port models that with
 * the reply context fields on xpc_dictionary_t (msg_mode, reply_port,
 * reply_disposition) plus the SPI-only storage added for this slice:
 * reply_msg_id / has_reply_msg_id and remote_connection.  Where Apple
 * faults (api_misuse) on misuse, this port returns the neutral value --
 * see each function.
 */

#include "xpc_internal.h"

#pragma mark - Pointer accessors

void
xpc_dictionary_set_pointer(xpc_object_t xdict, const char *key, void *ptr)
{
    xpc_object_t obj = xpc_pointer_create(ptr);

    xpc_dictionary_set_value(xdict, key, obj);
    xpc_release(obj);
}

void *
xpc_dictionary_get_pointer(xpc_object_t xdict, const char *key)
{
    /* Apple returns the xpc pointer OBJECT (the value object), not the
     * wrapped pointer: the get_value() result survives the type check
     * unchanged and is returned verbatim.  Callers unwrap with
     * xpc_pointer_get_value(). */
    xpc_object_t obj = xpc_dictionary_get_value(xdict, key);

    if (!obj || !XPC_OBJECT_CHECK(obj, &_xpc_type_pointer)) {
        return NULL;
    }
    return (void *)obj;
}

#pragma mark - Partial-port values (no counterpart object in this tree)

void
xpc_dictionary_set_value_with_key_string_cache(xpc_object_t xdict,
    const char *key, xpc_object_t value, xpc_object_t string_cache)
{
    /* Apple keys a caller-supplied string-cache object off the dictionary
     * so the wire serializer can skip re-hashing keys.  This tree has no
     * such cache object, so the cache argument is accepted and ignored:
     * the store behaves exactly like xpc_dictionary_set_value(). */
    (void)string_cache;
    xpc_dictionary_set_value(xdict, key, value);
}

xpc_object_t
_xpc_dictionary_get_transaction(xpc_object_t xdict)
{
    /* Apple returns the message-transaction object attached to a received
     * request (mode 1) or a reply (mode 2).  This tree models transactions
     * only as void begin/end depth counters (xpc_transaction.c), never as
     * dictionary envelope state, so no dictionary carries one. */
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return NULL;
    return NULL;
}

#pragma mark - Reply-msg-id trio

uint32_t
_xpc_dictionary_get_reply_msg_id(xpc_object_t xdict)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return 0;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
    if (d->msg_mode == 0) return 0;
    return d->has_reply_msg_id ? d->reply_msg_id : 0;
}

void
_xpc_dictionary_set_reply_msg_id(xpc_object_t xdict, uint32_t msg_id)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
    if (d->msg_mode == 0) return;   /* no reply context: Apple faults */
    d->reply_msg_id = msg_id;
    d->has_reply_msg_id = true;
}

uint32_t
_xpc_dictionary_extract_reply_msg_id(xpc_object_t xdict)
{
    uint32_t msg_id = _xpc_dictionary_get_reply_msg_id(xdict);

    if (XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) {
        xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
        if (d->msg_mode != 0) {
            d->reply_msg_id = 0;
            d->has_reply_msg_id = false;
        }
    }
    return msg_id;
}

#pragma mark - Reply port

xpc_object_t
_xpc_dictionary_create_reply_with_port(mach_port_t port)
{
    /* Apple's SPI mints an empty mode-2 dictionary and plants the caller's
     * reply port on it, so the caller can send back on an arbitrary right
     * without having received a request first.  The reply-disposition is
     * MOVE_SEND_ONCE, matching the send(-once) right callers pass here. */
    xpc_object_t rp = xpc_dictionary_create(NULL, NULL, 0);
    if (!rp) return NULL;
    xpc_dictionary_t *r = XPC_CAST(xpc_dictionary_t, rp);
    r->msg_mode = 2;
    r->reply_port = port;
    r->reply_disposition = MACH_MSG_TYPE_MOVE_SEND_ONCE;
    return rp;
}

mach_port_t
_xpc_dictionary_extract_reply_port(xpc_object_t xdict)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return MACH_PORT_NULL;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
    if (d->msg_mode == 0) return MACH_PORT_NULL;
    return d->reply_port;
}

void
xpc_dictionary_send_reply_4SWIFT(xpc_object_t request, xpc_object_t reply)
{
    /* Apple's Swift-facing reply path: if the reply is already a mode-2
     * dictionary (e.g. minted by create_reply_with_port()), ship it as-is;
     * otherwise wrap `reply`'s contents in a fresh reply built from
     * `request`'s context and send that, leaving `reply` untouched.  Soft
     * no-op when the inputs do not carry a usable reply context. */
    if (!XPC_OBJECT_CHECK(request, &_xpc_type_dictionary) ||
        !XPC_OBJECT_CHECK(reply, &_xpc_type_dictionary)) {
        return;
    }
    xpc_dictionary_t *rid = XPC_CAST(xpc_dictionary_t, request);
    xpc_dictionary_t *rpd = XPC_CAST(xpc_dictionary_t, reply);
    if (rid->msg_mode == 1 && rpd->msg_mode == 2) {
        xpc_dictionary_send_reply(reply);
        return;
    }
    xpc_object_t r = xpc_dictionary_create_reply(request);
    if (!r) return;
    xpc_dictionary_apply(reply, ^bool(const char *key, xpc_object_t value) {
        xpc_dictionary_set_value(r, key, value);
        return true;
    });
    xpc_dictionary_send_reply(r);
    xpc_release(r);
}

#pragma mark - Mach-right extraction

mach_port_t
xpc_dictionary_extract_mach_recv(xpc_object_t xdict, const char *key)
{
    /* Single-use: ownership of the receive right moves to the caller, and
     * the object becomes an inert shell (Apple faults on a second extract;
     * this port returns MACH_PORT_NULL). */
    xpc_object_t obj = xpc_dictionary_get_value(xdict, key);

    if (!obj || !XPC_OBJECT_CHECK(obj, &_xpc_type_mach_recv)) {
        return MACH_PORT_NULL;
    }
    return xpc_mach_recv_extract_right(obj);
}

mach_port_t
_xpc_dictionary_extract_mach_send(xpc_object_t xdict, const char *key)
{
    /* Single-use by SWP on Apple (a -2 right-name sentinel traps a double
     * extract); this port clears the port and returns MACH_PORT_NULL on a
     * second extract.  Extracting also drops the dispose flag so releasing
     * the shell no longer deallocates the moved right. */
    xpc_object_t obj = xpc_dictionary_get_value(xdict, key);

    if (!obj || !XPC_OBJECT_CHECK(obj, &_xpc_type_mach_send)) {
        return MACH_PORT_NULL;
    }
    xpc_mach_send_t *m = XPC_CAST(xpc_mach_send_t, obj);
    mach_port_t port = m->port;
    m->port = MACH_PORT_NULL;
    m->dispose = false;
    return port;
}

#pragma mark - Remote connection

xpc_connection_t
xpc_dictionary_get_connection(xpc_object_t xdict)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return NULL;
    return (xpc_connection_t)XPC_CAST(xpc_dictionary_t, xdict)->remote_connection;
}

void
_xpc_dictionary_set_remote_connection(xpc_object_t xdict,
    xpc_connection_t connection)
{
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, xdict);
    if (d->msg_mode != 1) return;   /* only a received request: Apple faults */

    xpc_object_t old = (xpc_object_t)d->remote_connection;
    d->remote_connection = (struct _xpc_connection_s *)(connection
        ? xpc_retain((xpc_object_t)connection) : NULL);
    if (old) xpc_release(old);
}

#pragma mark - Description

char *
xpc_dictionary_copy_basic_description(xpc_object_t xdict)
{
    /* Apple writes a printf into a 256-byte stack buffer and returns a
     * malloc'd copy; this port reuses the tree's description appender,
     * which already returns a fresh allocation. */
    if (!XPC_OBJECT_CHECK(xdict, &_xpc_type_dictionary)) return NULL;
    return xpc_description_create(xdict);
}