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
 * xpc_entitlements.c — entitlement SPI (launchd-facing).
 *
 * macOS provides xpc_copy_entitlement_for_token and
 * xpc_copy_entitlements_for_pid from /usr/lib/system/libxpc.dylib; we
 * export the same pair because a `-lxpc` link against our dylib shadows
 * the SDK's libSystem re-export for xpc_* names, so launchd and friends
 * must be able to bind these from us.
 *
 * The real blob is fetched with proc_pidinfo(PROC_PIDT_ENTITLEMENTS) and
 * decoded with the component wire deserializer.  When no entitlements are
 * available (or the process is gone) the calls return NULL, which launchd
 * already treats as the "no entitlements" case.
 */

#include "xpc_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <libproc.h>
#include <sys/proc_info.h>

/*
 * PROC_PIDT_ENTITLEMENTS is private to xnu's sys/proc_info.h (the public
 * SDK stops at the fds/region flavors), so pin the value Apple uses here.
 */
#ifndef PROC_PIDT_ENTITLEMENTS
#define PROC_PIDT_ENTITLEMENTS 32
#endif

xpc_object_t
xpc_copy_entitlements_for_pid(int pid)
{
    xpc_object_t result = NULL;
    void *buf = NULL;
    int sz;

    if (pid <= 0) {
        return NULL;
    }

    sz = proc_pidinfo(pid, PROC_PIDT_ENTITLEMENTS, 0, NULL, 0);
    if (sz <= 0) {
        /*
         * The flavor may not answer a size-only query; fall back to a
         * generously sized buffer on the first call and truncate rather
         * than fail outright.
         */
        sz = 1 << 20;
    }
    buf = malloc((size_t)sz);
    if (!buf) {
        return NULL;
    }
    int got = proc_pidinfo(pid, PROC_PIDT_ENTITLEMENTS, 0, buf, sz);
    if (got <= 0) {
        free(buf);
        return NULL;
    }
    result = xpc_wire_deserialize(buf, (size_t)got);
    free(buf);
    return result;
}

xpc_object_t
xpc_copy_entitlement_for_token(const char *key, audit_token_t *token)
{
    xpc_object_t dict, value = NULL;

    if (!key || !token) {
        return NULL;
    }
    /* Canonical 8-word layout: pid -> val[5] (see xpc_routines.c). */
    dict = xpc_copy_entitlements_for_pid((pid_t)token->val[5]);
    if (!dict) {
        return NULL;
    }
    value = xpc_dictionary_get_value(dict, key);
    if (value) {
        xpc_retain(value);
    }
    xpc_release(dict);
    return value;
}