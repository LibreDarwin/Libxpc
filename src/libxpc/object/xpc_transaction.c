/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xpc_transaction.c — xpc_transaction_begin()/xpc_transaction_end().
 *
 * These manage the XPC-runtime transaction reference count, mirroring
 * Apple's __xpc_transaction_begin()/__xpc_transaction_end().  A service
 * must stay alive while a transaction is outstanding; when the count is
 * drained to zero the runtime may consider the service idle and exit it
 * (per the API documentation in xpc/xpc.h).
 *
 * Apple's implementations additionally:
 *   - guard the counter with an os_unfair_lock and fault on wraparound or
 *     underflow through __xpc_api_misuse("Underflow of transaction count."),
 *   - notify the vproc layer (proc_set_dirty) across the 0<->1 boundaries
 *     so the task's dirty/flush accounting stays coherent, and
 *   - install a SIGTERM dispatch source and, once the runtime decides the
 *     service should exit, _exit(0) when the count returns to zero.
 * This port preserves the observable contract -- a balanced reference count
 * with the same fault on underflow -- and omits the internal dirty-account
 * and SIGTERM machinery, which nothing in this tree consumes.
 */

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "xpc_internal.h"

static atomic_uint_fast32_t _xpc_transaction_depth;	/* outstanding txns */

static void
_xpc_transaction_misuse(void)
{
    /* Apple faults here with the same text via __xpc_asprintf(); this tree
     * has no os_log/crash machinery, so report on stderr and abort. */
    fprintf(stderr, "Underflow of transaction count.\n");
    abort();
}

void
xpc_transaction_begin(void)
{
    uint32_t old = atomic_fetch_add_explicit(&_xpc_transaction_depth, 1,
        memory_order_relaxed);
    if (old == UINT32_MAX) {
        /* Wraparound would roll the count of outstanding transactions past
         * zero; no caller can legitimately hold 2^32 of them. */
        _xpc_transaction_misuse();
    }
}

void
xpc_transaction_end(void)
{
    uint32_t old = atomic_fetch_sub_explicit(&_xpc_transaction_depth, 1,
        memory_order_relaxed);
    if (old == 0) {
        /* Ended a transaction that was never begun. */
        _xpc_transaction_misuse();
    }
}