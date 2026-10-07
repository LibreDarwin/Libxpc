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
 * xpc_value.c — scalar value types for the reimplemented XPC framework.
 *
 * null, bool, int64, uint64, double, and date all share the
 * xpc_scalar_t storage (header + inline union member).
 */

#include "xpc_internal.h"

#include <time.h>

#pragma mark - null

xpc_object_t
xpc_null_create(void)
{
    return xpc_object_alloc_scalar(&_xpc_type_null);
}

#pragma mark - bool

xpc_object_t
xpc_bool_create(bool value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_bool));
    if (o) o->v.bval = value;
    return (xpc_object_t)o;
}

const struct _xpc_scalar_s _xpc_bool_true_val = {
    .hdr = XPC_OBJECT_HEADER(&_xpc_type_bool),
    .v = { .bval = true }
};
const struct _xpc_scalar_s _xpc_bool_false_val = {
    .hdr = XPC_OBJECT_HEADER(&_xpc_type_bool),
    .v = { .bval = false }
};

xpc_object_t _xpc_bool_true = (xpc_object_t)&_xpc_bool_true_val;
xpc_object_t _xpc_bool_false = (xpc_object_t)&_xpc_bool_false_val;

bool
xpc_bool_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_bool)) return false;
    return XPC_CAST(xpc_scalar_t, obj)->v.bval;
}

/*
 * xpc_bool_set_value(obj, value) — SPI value setter.  Apple's
 * implementation stores blindly (tbz disable_disasm offsets); we type-check
 * first so misuse fails loudly but silently, matching the port's other
 * accessor conventions.
 */
void
xpc_bool_set_value(xpc_object_t obj, bool value)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_bool)) return;
    XPC_CAST(xpc_scalar_t, obj)->v.bval = value;
}

#pragma mark - int64

xpc_object_t
xpc_int64_create(int64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_int64));
    if (o) o->v.i64 = value;
    return (xpc_object_t)o;
}

int64_t
xpc_int64_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_int64)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.i64;
}

void
xpc_int64_set_value(xpc_object_t obj, int64_t value)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_int64)) return;
    XPC_CAST(xpc_scalar_t, obj)->v.i64 = value;
}

#pragma mark - uint64

xpc_object_t
xpc_uint64_create(uint64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_uint64));
    if (o) o->v.u64 = value;
    return (xpc_object_t)o;
}

uint64_t
xpc_uint64_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_uint64)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.u64;
}

#pragma mark - double

xpc_object_t
xpc_double_create(double value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_double));
    if (o) o->v.dbl = value;
    return (xpc_object_t)o;
}

double
xpc_double_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_double)) return 0.0;
    return XPC_CAST(xpc_scalar_t, obj)->v.dbl;
}

void
xpc_double_set_value(xpc_object_t obj, double value)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_double)) return;
    XPC_CAST(xpc_scalar_t, obj)->v.dbl = value;
}

#pragma mark - date

xpc_object_t
xpc_date_create(int64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_date));
    if (o) o->v.date_ns = value;
    return (xpc_object_t)o;
}

int64_t
xpc_date_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_date)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.date_ns;
}

xpc_object_t
xpc_date_create_from_current(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return NULL;
    return xpc_date_create((int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec);
}

xpc_object_t
xpc_create_from_plist(const void *data, size_t length)
{
    (void)data;
    (void)length;
    return NULL;
}

xpc_object_t
xpc_create_from_plist_with_string_cache(const void *data, size_t length, xpc_object_t cache)
{
    (void)data;
    (void)length;
    (void)cache;
    return NULL;
}

xpc_object_t
xpc_get_service_identifier_for_token(audit_token_t *token)
{
    (void)token;
    return NULL;
}

#pragma mark - pointer

/*
 * xpc_pointer_create(value) / xpc_pointer_get_value(obj) — SPI opaque
 * pointer wrapping.  Apple allocates an 8-byte base object
 * (__xpc_base_create(type, 8)); we reuse the scalar storage union.
 * Pointer objects are never serialized; hitting the wire hits the
 * serializer's default (abort) path, as on Apple.
 */
xpc_object_t
xpc_pointer_create(void *value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_pointer));
    if (o) o->v.ptr = value;
    return (xpc_object_t)o;
}

void *
xpc_pointer_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_pointer)) return NULL;
    return XPC_CAST(xpc_scalar_t, obj)->v.ptr;
}
