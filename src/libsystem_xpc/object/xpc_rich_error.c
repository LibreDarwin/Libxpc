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
 * xpc_rich_error.c — the rich-error type (xpc/rich_error.h).
 *
 * A rich error is a lightweight diagnostic object (type
 * XPC_TYPE_RICH_ERROR) carrying a freeable description and a "can retry"
 * flag.  Session APIs produce these on failure through their error_out
 * parameters; the session's cancel handler receives whatever rich error
 * explained the teardown.
 */

#include "xpc_internal.h"

xpc_rich_error_t
xpc_rich_error_create(const char *desc, bool can_retry)
{
    struct _xpc_rich_error_s *r =
        (struct _xpc_rich_error_s *)(void *)xpc_object_alloc(
            &_xpc_type_rich_error, sizeof(*r));
    if (!r) return NULL;
    r->desc = desc ? strdup(desc) : NULL;
    r->can_retry = can_retry;
    return (xpc_rich_error_t)(void *)r;
}

int
xpc_rich_error_can_retry(xpc_object_t error)
{
    if (!XPC_OBJECT_CHECK(error, &_xpc_type_rich_error)) return 0;
    struct _xpc_rich_error_s *r = (struct _xpc_rich_error_s *)(void *)error;
    return r->can_retry ? 1 : 0;
}

char *
xpc_rich_error_copy_description(xpc_object_t error)
{
    if (!XPC_OBJECT_CHECK(error, &_xpc_type_rich_error)) return NULL;
    struct _xpc_rich_error_s *r = (struct _xpc_rich_error_s *)(void *)error;
    return r->desc ? strdup(r->desc) : NULL;
}