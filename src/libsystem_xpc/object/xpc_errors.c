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
 * xpc_errors.c — the connection error singletons (XPC_ERROR_*).
 *
 * Each is declared in the public headers as const struct _xpc_dictionary_s
 * (Apple's storage type) but carries isa = &_xpc_type_error so
 * xpc_get_type() reports XPC_TYPE_ERROR and message handlers branch on the
 * event kind correctly.  refs == 0 marks them immortal:
 * xpc_retain()/xpc_release() never touch them (see xpc_object.c).
 */

#include "xpc_internal.h"

#define XPC_ERROR_DESC(obj, text)                                    \
static struct _xpc_string_s _##obj##_str = {                         \
    .hdr = { .isa = &_xpc_type_string, .refs = 0 },                  \
    .data = (char *)text,                                            \
    .length = sizeof(text) - 1,                                      \
};                                                                   \
static char *_##obj##_keys[] = { (char *)"desc", NULL };             \
static xpc_object_t _##obj##_vals[] =                                \
    { (xpc_object_t)&_##obj##_str, NULL };                           \
const struct _xpc_dictionary_s obj = {                               \
    .hdr = { .isa = &_xpc_type_error, .refs = 0 },                   \
    .keys = _##obj##_keys,                                           \
    .values = _##obj##_vals,                                         \
};

XPC_ERROR_DESC(_xpc_error_connection_interrupted, "Connection interrupted")
XPC_ERROR_DESC(_xpc_error_connection_invalid, "Connection invalid")
XPC_ERROR_DESC(_xpc_error_termination_imminent,
    "Service should not have launched")
XPC_ERROR_DESC(_xpc_error_peer_code_signing_requirement,
    "Code signing requirement not satisfied")