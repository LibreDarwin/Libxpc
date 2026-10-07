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
 * xpc_binprefs.c — binary architecture preferences (xpc_binprefs SPI).
 *
 * A binprefs block records up to four (cpu_type, cpu_subtype) pairs that
 * posix_spawn consults when picking a slice.  The struct layout and the
 * semantics of every entry point are pinned against Apple's libxpc (see
 * struct _xpc_binprefs_s in xpc_internal.h).  Indexing past the recorded
 * count, or a failing posix_spawnattr_setarchpref_np(), faults with the
 * same phrasing Apple asserts; this tree has no os_log/crash machinery,
 * so it reports on stderr and aborts, like xpc_transaction's underflow.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpc_internal.h"

static void
xpc_binprefs_fault(const char *expr)
{
    fprintf(stderr, "assertion failure: \"%s\"\n", expr);
    abort();
}

xpc_binprefs_t
xpc_binprefs_alloc(void)
{
    struct _xpc_binprefs_s *bp = calloc(1, sizeof(*bp));
    return (xpc_binprefs_t)(void *)bp;
}

void
xpc_binprefs_init(xpc_binprefs_t binprefs)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return;                    /* Apple dereferences and crashes */
    memset(bp, 0, sizeof(*bp));
}

xpc_binprefs_t
xpc_binprefs_copy(xpc_binprefs_t binprefs)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return NULL;               /* Apple's __xpc_memdup faults on NULL */
    struct _xpc_binprefs_s *copy = calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    memcpy(copy, bp, sizeof(*copy));
    return (xpc_binprefs_t)(void *)copy;
}

void
xpc_binprefs_add(xpc_binprefs_t binprefs, int32_t cpu_type, int32_t cpu_subtype)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return;                    /* Apple dereferences and crashes */
    if (bp->xb_count >= 4) {
        /* Apple logs a non-fatal _os_assumes_log(0) here and drops the
         * entry; there is no observable state change, so this port is
         * silent. */
        return;
    }
    bp->xb_cpu_types[bp->xb_count] = cpu_type;
    bp->xb_cpu_subtypes[bp->xb_count] = cpu_subtype;
    bp->xb_count++;
}

uint32_t
xpc_binprefs_count(xpc_binprefs_t binprefs)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return 0;                  /* Apple dereferences and crashes */
    return bp->xb_count;
}

int32_t
xpc_binprefs_cpu_type(xpc_binprefs_t binprefs, uint32_t index)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp || index >= bp->xb_count)
        xpc_binprefs_fault("i < self->xb_count");
    return bp->xb_cpu_types[index];
}

int32_t
xpc_binprefs_cpu_subtype(xpc_binprefs_t binprefs, uint32_t index)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp || index >= bp->xb_count)
        xpc_binprefs_fault("i < self->xb_count");
    return bp->xb_cpu_subtypes[index];
}

bool
xpc_binprefs_equal(xpc_binprefs_t a, xpc_binprefs_t b)
{
    struct _xpc_binprefs_s *x = (struct _xpc_binprefs_s *)(void *)a;
    struct _xpc_binprefs_s *y = (struct _xpc_binprefs_s *)(void *)b;
    bool either_present = a != NULL || b != NULL;
    if (!a || !b) return either_present; /* both NULL: false, one NULL: true */
    if (x->xb_count != y->xb_count) return false;
    for (uint32_t i = 0; i < x->xb_count; i++) {
        if (x->xb_cpu_types[i] != y->xb_cpu_types[i]) return false;
        if (x->xb_cpu_subtypes[i] != y->xb_cpu_subtypes[i]) return false;
    }
    return true;
}

bool
xpc_binprefs_is_noop(xpc_binprefs_t binprefs)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return true;               /* no prefs at all is a no-op */
    if (bp->xb_count == 0) return true;
    /* Apple quirk: a leading CPU_TYPE_ANY (-1) is NOT a no-op. */
    return bp->xb_cpu_types[0] != -1;
}

static void
bp_append(char *buf, size_t bufsz, size_t *off, const char *fmt, ...)
{
    va_list ap;
    if (*off >= bufsz - 1) return;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, bufsz - *off, fmt, ap);
    va_end(ap);
    if (n > 0) *off += (size_t)n;
    if (*off >= bufsz - 1) *off = bufsz - 1; /* clamp so the buffer stays NUL-terminated */
}

char *
xpc_binprefs_copy_description(xpc_binprefs_t binprefs)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return strdup("(null)");
    char buf[160];
    size_t off = 0;
    bp_append(buf, sizeof(buf), &off, "%u: [", bp->xb_count);
    for (uint32_t i = 0; i < bp->xb_count; i++) {
        if (i != 0) bp_append(buf, sizeof(buf), &off, ", ");
        bp_append(buf, sizeof(buf), &off, "%d.%d",
            bp->xb_cpu_types[i], bp->xb_cpu_subtypes[i]);
    }
    bp_append(buf, sizeof(buf), &off, "]");
    return strdup(buf);
}

void
xpc_binprefs_set_psattr(xpc_binprefs_t binprefs, posix_spawnattr_t *psattr)
{
    struct _xpc_binprefs_s *bp = (struct _xpc_binprefs_s *)(void *)binprefs;
    if (!bp) return;                    /* Apple dereferences and crashes */
    int rv = posix_spawnattr_setarchpref_np(psattr, bp->xb_count,
        bp->xb_cpu_types, bp->xb_cpu_subtypes, NULL);
    if (rv != 0)
        xpc_binprefs_fault("posix_spawnattr_setarchpref_np(psattr, "
            "self->xb_count, self->xb_cpu_types, self->xb_cpu_subtypes, "
            "(void*)0)");
}