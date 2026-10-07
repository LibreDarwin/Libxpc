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
 * xpc_mach_recv.c — Mach receive-right values for the reimplemented XPC
 * framework.
 *
 * A receive right can travel inside a dictionary so the receiver can
 * keep sending after a request completes (xpc_event_channel_check_in is
 * one such path: launchd hands its "port" back over the wire as a receive
 * right).  Mirrors Apple: xpc_mach_recv_create() takes ownership of the
 * receive right and xpc_dictionary_set_mach_recv() wraps it for shipping.
 *
 * Transport is single-use, matching apple __xpc_mach_recv_serialize: the
 * wire descriptor for a mach-recv value is MOVE_RECEIVE, so serialization
 * consumes the source right (extract), and the receiving end adopts a
 * fresh copy via xpc_mach_recv_create_owned().  A value that is neither
 * sent nor extracted destroys the receive right on release.
 */

#include "xpc_internal.h"

static xpc_object_t
xpc_mach_recv_create_internal(mach_port_t port, bool dispose)
{
    xpc_mach_recv_t *m = XPC_CAST(xpc_mach_recv_t,
        xpc_object_alloc(&_xpc_type_mach_recv, sizeof(xpc_mach_recv_t)));
    if (!m) return NULL;
    m->port = port;
    m->dispose = dispose;
    return (xpc_object_t)m;
}

xpc_object_t
xpc_mach_recv_create(mach_port_t port)
{
    /* Owns the receive right from birth (Apple's create, and therefore
     * xpc_dictionary_set_mach_recv(), take it over). */
    return xpc_mach_recv_create_internal(port, true);
}

xpc_object_t
xpc_mach_recv_create_owned(mach_port_t port)
{
    return xpc_mach_recv_create_internal(port, true);
}

mach_port_t
xpc_mach_recv_extract_right(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_mach_recv)) return MACH_PORT_NULL;
    xpc_mach_recv_t *m = XPC_CAST(xpc_mach_recv_t, obj);
    mach_port_t port = m->port;
    m->port = MACH_PORT_NULL;   /* single-use: ownership moves to the caller */
    m->dispose = false;
    return port;
}

mach_port_t
__xpc_mach_recv_get_name(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_mach_recv)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_mach_recv_t, obj)->port;
}

void
xpc_dictionary_set_mach_recv(xpc_object_t dict, const char *key,
    mach_port_t port)
{
    xpc_object_t obj = xpc_mach_recv_create(port);
    if (!obj) return;
    xpc_dictionary_set_value(dict, key, obj);
    xpc_release(obj);
}