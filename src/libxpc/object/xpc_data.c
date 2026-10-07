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
 * xpc_data.c — data (blob) values for the reimplemented XPC framework.
 *
 * The public user API (xpc_data_create) copies its input, matching
 * Apple's semantics; the internal one moves ownership.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_data_create(const void *bytes, size_t length)
{
    if (length > 0 && !bytes) return NULL;

    xpc_data_t *d = XPC_CAST(xpc_data_t,
        xpc_object_alloc(&_xpc_type_data, sizeof(xpc_data_t)));
    if (!d) return NULL;

    if (length == 0) {
        d->data = NULL;
        d->length = 0;
        return (xpc_object_t)d;
    }

    uint8_t *copy = malloc(length);
    if (!copy) {
        free(d);
        return NULL;
    }
    memcpy(copy, bytes, length);
    d->data = copy;
    d->length = length;
    return (xpc_object_t)d;
}

/*
 * xpc_data_create_with_dispatch_data(ddata) — data backed by a dispatch
 * data object.  Apple's libxpc keeps a dispatch_retain'ed reference and
 * maps bytes lazily (the point of the create_map call is deliberately
 * undefined).  This port coalesces eagerly into an owned buffer: the result
 * is a plain xpc_data_t with no dispatch lifetime, so the caller may
 * release ddata immediately and the value serializes identically.
 */
xpc_object_t
xpc_data_create_with_dispatch_data(dispatch_data_t ddata)
{
    if (!ddata) return NULL;

    size_t length = dispatch_data_get_size(ddata);
    xpc_data_t *d = XPC_CAST(xpc_data_t,
        xpc_object_alloc(&_xpc_type_data, sizeof(xpc_data_t)));
    if (!d) return NULL;

    if (length == 0) {
        d->data = NULL;
        d->length = 0;
        return (xpc_object_t)d;
    }

    const void *bytes = NULL;
    size_t len = 0;
    dispatch_data_t contig = dispatch_data_create_map(ddata, &bytes, &len);
    if (!contig) {
        free(d);
        return NULL;
    }

    uint8_t *copy = malloc(len);
    if (!copy) {
        dispatch_release(contig);
        free(d);
        return NULL;
    }
    memcpy(copy, bytes, len);
    dispatch_release(contig);

    d->data = copy;
    d->length = len;
    return (xpc_object_t)d;
}

/* Internal: adopt an existing heap buffer (xpc_deserialize.c uses this). */
xpc_object_t
xpc_data_create_take(uint8_t *bytes, size_t length)
{
    xpc_data_t *d = XPC_CAST(xpc_data_t,
        xpc_object_alloc(&_xpc_type_data, sizeof(xpc_data_t)));
    if (!d) return NULL;
    d->data = bytes;
    d->length = length;
    return (xpc_object_t)d;
}

const void *
xpc_data_get_bytes_ptr(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_data)) return NULL;
    return XPC_CAST(xpc_data_t, obj)->data;
}

size_t
xpc_data_get_length(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_data)) return 0;
    return XPC_CAST(xpc_data_t, obj)->length;
}

size_t
xpc_data_get_bytes(xpc_object_t obj, void *buffer, size_t off, size_t length)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_data)) return 0;
    xpc_data_t *d = XPC_CAST(xpc_data_t, obj);
    if (off >= d->length) return 0;
    size_t avail = d->length - off;
    size_t n = length < avail ? length : avail;
    if (buffer && n > 0) memcpy(buffer, d->data + off, n);
    return n;
}