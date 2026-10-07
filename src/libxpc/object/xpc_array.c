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
 * xpc_array.c — array containers for the reimplemented XPC framework.
 *
 * Arrays hold strong references in insertion order; count == items
 * actually stored, capacity == allocated slot count.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_array_create(const xpc_object_t *objects, size_t count)
{
    xpc_array_t *a = XPC_CAST(xpc_array_t,
        xpc_object_alloc(&_xpc_type_array, sizeof(xpc_array_t)));
    if (!a) return NULL;

    if (count > 0) {
        size_t cap = count < 8 ? 8 : count;
        a->items = calloc(cap, sizeof(xpc_object_t));
        if (!a->items) {
            free(a);
            return NULL;
        }
        a->capacity = cap;
        if (objects) {
            for (size_t i = 0; i < count; i++) {
                a->items[i] = xpc_retain(objects[i]);
            }
        }
        a->count = count;
    }
    return (xpc_object_t)a;
}

static bool
xpc_array_grow(xpc_array_t *a, size_t need)
{
    if (need <= a->capacity) return true;
    size_t newcap = a->capacity ? a->capacity * 2 : 8;
    while (newcap < need) newcap *= 2;
    xpc_object_t *n = realloc(a->items, newcap * sizeof(xpc_object_t));
    if (!n) return false;
    memset(n + a->count, 0, (newcap - a->count) * sizeof(xpc_object_t));
    a->items = n;
    a->capacity = newcap;
    return true;
}

void
xpc_array_append_value(xpc_object_t array, xpc_object_t value)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array) || !value) return;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (!xpc_array_grow(a, a->count + 1)) return;
    a->items[a->count++] = xpc_retain(value);
}

void
xpc_array_set_value(xpc_object_t array, size_t index, xpc_object_t value)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (index == XPC_ARRAY_APPEND) { /* Apple: APPEND works for set_value too */
        xpc_array_append_value(array, value);
        return;
    }
    if (index >= a->count) return;   /* Apple: set only replaces existing */

    xpc_object_t old = a->items[index];
    a->items[index] = value ? xpc_retain(value) : NULL;
    xpc_release(old);
    if (!value) {   /* NULL value removes the slot */
        for (size_t i = index; i + 1 < a->count; i++) {
            a->items[i] = a->items[i + 1];
        }
        a->count--;
    }
}

xpc_object_t
xpc_array_get_value(xpc_object_t array, size_t index)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return NULL;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (index >= a->count) return NULL;
    return a->items[index];
}

size_t
xpc_array_get_count(xpc_object_t array)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return 0;
    return XPC_CAST(xpc_array_t, array)->count;
}

bool
xpc_array_apply(xpc_object_t array,
    bool (^applier)(size_t index, xpc_object_t value))
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array) || !applier) return false;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    for (size_t i = 0; i < a->count; i++) {
        if (!applier(i, a->items[i])) return false;
    }
    return true;
}

xpc_object_t
xpc_array_create_empty(void)
{
    return xpc_array_create(NULL, 0);
}

const char *
xpc_array_get_string(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_string)) return NULL;
    return ((xpc_string_t *)v)->data;
}

void
xpc_array_set_int64(xpc_object_t xarray, size_t index, int64_t value)
{
    xpc_object_t obj = xpc_int64_create(value);
    if (index == XPC_ARRAY_APPEND) {
        xpc_array_append_value(xarray, obj);
    } else {
        xpc_array_set_value(xarray, index, obj);
    }
    xpc_release(obj);
}

void
xpc_array_set_string(xpc_object_t xarray, size_t index, const char *string)
{
    xpc_object_t obj = xpc_string_create(string);
    if (index == XPC_ARRAY_APPEND) {
        xpc_array_append_value(xarray, obj);
    } else {
        xpc_array_set_value(xarray, index, obj);
    }
    xpc_release(obj);
}

/* --- primitive getters (Apple: wrong type / out of range -> 0 / NULL) --- */

bool
xpc_array_get_bool(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_bool)) return false;
    return XPC_CAST(xpc_scalar_t, v)->v.bval;
}

int64_t
xpc_array_get_int64(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_int64)) return 0;
    return XPC_CAST(xpc_scalar_t, v)->v.i64;
}

uint64_t
xpc_array_get_uint64(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_uint64)) return 0;
    return XPC_CAST(xpc_scalar_t, v)->v.u64;
}

double
xpc_array_get_double(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_double)) return 0.0;
    return XPC_CAST(xpc_scalar_t, v)->v.dbl;
}

int64_t
xpc_array_get_date(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_date)) return 0;
    return XPC_CAST(xpc_scalar_t, v)->v.date_ns;
}

const void *
xpc_array_get_data(xpc_object_t xarray, size_t index, size_t *length)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_data)) {
        if (length) *length = 0;
        return NULL;
    }
    xpc_data_t *d = XPC_CAST(xpc_data_t, v);
    if (length) *length = d->length;
    return d->data;
}

const uint8_t *
xpc_array_get_uuid(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_uuid)) return NULL;
    return XPC_CAST(xpc_uuid_t, v)->uuid;
}

xpc_object_t
xpc_array_get_array(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_array)) return NULL;
    return v;
}

xpc_object_t
xpc_array_get_dictionary(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_dictionary)) return NULL;
    return v;
}

/* --- primitive setters (XPC_ARRAY_APPEND appends, like set_int64) --- */

static void
xpc_array_set_scalar(xpc_object_t xarray, size_t index, xpc_object_t obj)
{
    if (index == XPC_ARRAY_APPEND) {
        xpc_array_append_value(xarray, obj);
    } else {
        xpc_array_set_value(xarray, index, obj);
    }
    xpc_release(obj);
}

void
xpc_array_set_bool(xpc_object_t xarray, size_t index, bool value)
{
    xpc_array_set_scalar(xarray, index, xpc_bool_create(value));
}

void
xpc_array_set_uint64(xpc_object_t xarray, size_t index, uint64_t value)
{
    xpc_array_set_scalar(xarray, index, xpc_uint64_create(value));
}

void
xpc_array_set_double(xpc_object_t xarray, size_t index, double value)
{
    xpc_array_set_scalar(xarray, index, xpc_double_create(value));
}

void
xpc_array_set_date(xpc_object_t xarray, size_t index, int64_t value)
{
    xpc_array_set_scalar(xarray, index, xpc_date_create(value));
}

void
xpc_array_set_data(xpc_object_t xarray, size_t index, const void *bytes,
    size_t length)
{
    xpc_array_set_scalar(xarray, index, xpc_data_create(bytes, length));
}

void
xpc_array_set_uuid(xpc_object_t xarray, size_t index, const uuid_t uuid)
{
    xpc_array_set_scalar(xarray, index, xpc_uuid_create(uuid));
}

/* --- file descriptors and connections --- */

void
xpc_array_set_fd(xpc_object_t xarray, size_t index, int fd)
{
    xpc_object_t obj = xpc_fd_create(fd);
    if (!obj) return;
    xpc_array_set_scalar(xarray, index, obj);
}

int
xpc_array_dup_fd(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_fd)) return -1;
    return xpc_fd_dup(v);
}

void
xpc_array_set_connection(xpc_object_t xarray, size_t index,
    xpc_object_t connection)
{
    /* Stores an endpoint holding the connection's port; the array does not
     * retain the connection itself (Apple xpc_array_set_connection(3)). */
    if (!XPC_OBJECT_CHECK(connection, &_xpc_type_connection)) return;
    struct _xpc_connection_s *cc = (struct _xpc_connection_s *)(void *)connection;
    xpc_object_t obj = xpc_endpoint_create(cc->self_port);
    if (!obj) return;
    xpc_array_set_scalar(xarray, index, obj);
}

xpc_object_t
xpc_array_create_connection(xpc_object_t xarray, size_t index)
{
    xpc_object_t v = xpc_array_get_value(xarray, index);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_endpoint)) return NULL;
    xpc_connection_t conn =
        xpc_connection_create_from_endpoint((xpc_endpoint_t)(void *)v);
    return (xpc_object_t)(void *)conn;
}
