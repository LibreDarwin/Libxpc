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
 * xpc_spawnattr_serialize.c — spawnattr wire-format pack/unpack SPI.
 *
 * These are the primitives an xpc_spawnattr_t uses to serialize itself into
 * a caller-supplied blob.  The blob's fixed metadata occupies 293 bytes
 * (0x125); the string/byte section begins right after it, so every pack
 * writes at 293 + cursor and every unpack reads back from the same base.
 * Two u32 fields in the blob header (at +68 and +72) record the binprefs
 * count and the offset of the binprefs section within the string/byte
 * region.
 *
 * All semantics — the cursor/space accounting (string advances by
 * strlen+1, string-fragment by strlen so fragments concatenate without a
 * gap, bytes by len), the bounds math of the unpack helpers, and the
 * binprefs section layout ((type, subtype) pairs of 4+4 bytes) — are
 * pinned against Apple's libxpc disassembly.  The unpack bounds compare
 * `limit - offset` against the remaining length and wrap exactly like the
 * original when offset exceeds limit.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpc_internal.h"

#define SPAWNATTR_STRING_BASE       293u
#define BINPREFS_COUNT_OFFSET       68u
#define BINPREFS_START_OFFSET       72u

static void
spawnattr_wr32(void *p, uint32_t v)
{
    memcpy(p, &v, sizeof v);
}

static uint32_t
spawnattr_rd32(const void *p)
{
    uint32_t v;

    memcpy(&v, p, sizeof v);
    return v;
}

static void
spawnattr_fault(const char *expr)
{
    fprintf(stderr, "assertion failure: \"%s\"\n", expr);
    abort();
}

void
_xpc_spawnattr_pack_string(void *buf, uint32_t *count, uint64_t *space,
    const char *str)
{
    size_t inc = (size_t)strlen(str) + 1;

    strcpy((char *)buf + *count + SPAWNATTR_STRING_BASE, str);
    *count = (uint32_t)((size_t)*count + inc);
    *space -= inc;
}

void
_xpc_spawnattr_pack_string_fragment(void *buf, uint32_t *count,
    uint64_t *space, const char *fragment)
{
    size_t len = strlen(fragment);

    strcpy((char *)buf + *count + SPAWNATTR_STRING_BASE, fragment);
    *count = (uint32_t)((size_t)*count + len);
    *space -= len;
}

const char *
_xpc_spawnattr_unpack_string(const void *buf, size_t limit, uint32_t offset)
{
    size_t remaining;
    const char *s;
    size_t len;

    if (limit <= offset) {
        return NULL;
    }
    remaining = limit - (size_t)offset;
    s = (const char *)buf + (size_t)offset + SPAWNATTR_STRING_BASE;
    len = strnlen(s, remaining);
    if (len + 1 > remaining) {
        return NULL;
    }
    return s;
}

void
_xpc_spawnattr_pack_bytes(void *buf, uint32_t *count, uint64_t *space,
    const void *bytes, uint32_t len)
{
    memcpy((char *)buf + *count + SPAWNATTR_STRING_BASE, bytes, len);
    *count = (uint32_t)((size_t)*count + len);
    *space -= len;
}

const char *
_xpc_spawnattr_unpack_bytes(const void *buf, size_t limit, uint32_t offset,
    uint32_t len)
{
    if ((limit - (size_t)offset) < (size_t)len || limit <= offset) {
        return NULL;
    }
    return (const char *)buf + (size_t)offset + SPAWNATTR_STRING_BASE;
}

const char *
_xpc_spawnattr_unpack_strings(const void *buf, size_t limit, uint32_t offset,
    const char **strings, uint32_t count)
{
    size_t remaining;
    const char *s;
    uint32_t i;

    if (limit <= offset) {
        return NULL;
    }
    if (count == 0) {
        return strings[0];
    }
    remaining = limit - (size_t)offset;
    s = (const char *)buf + (size_t)offset + SPAWNATTR_STRING_BASE;
    for (i = 0; i < count; i++) {
        size_t len = strnlen(s, remaining);

        if (len + 1 > remaining) {
            return NULL;
        }
        strings[i] = s;
        offset = (uint32_t)((size_t)offset + len + 1);
        s = (const char *)buf + (size_t)offset + SPAWNATTR_STRING_BASE;
    }
    return strings[0];
}

size_t
_xpc_spawnattr_binprefs_size(xpc_binprefs_t binprefs)
{
    return (size_t)xpc_binprefs_count(binprefs) * 8u;
}

void
_xpc_spawnattr_binprefs_pack(void *buf, xpc_binprefs_t binprefs,
    uint32_t *pk, uint64_t *space)
{
    uint32_t count = xpc_binprefs_count(binprefs);
    uint32_t i;

    spawnattr_wr32((char *)buf + BINPREFS_COUNT_OFFSET, count);
    if (count == 0) {
        spawnattr_wr32((char *)buf + BINPREFS_START_OFFSET, 0);
        return;
    }

    spawnattr_wr32((char *)buf + BINPREFS_START_OFFSET, *pk);
    for (i = 0; i < count; i++) {
        uint8_t *p = (uint8_t *)buf + *pk + SPAWNATTR_STRING_BASE + 4u + i * 8u;

        spawnattr_wr32(p - 4, (uint32_t)xpc_binprefs_cpu_type(binprefs, i));
        spawnattr_wr32(p, (uint32_t)xpc_binprefs_cpu_subtype(binprefs, i));
    }

    /*
     * Apple re-queries the count and faults if it changed underneath the
     * pack.  xpc_binprefs_add() drops past four entries, so the count is
     * stable in practice; keep the check for parity.
     */
    if ((size_t)xpc_binprefs_count(binprefs) != (size_t)count) {
        spawnattr_fault("xpc_spawnattr_binprefs_pack: count changed");
    }

    *pk = (uint32_t)((size_t)*pk + (size_t)count * 8u);
    *space -= (size_t)count * 8u;
}

xpc_binprefs_t
_xpc_spawnattr_binprefs_unpack(const void *buf, size_t limit)
{
    uint32_t count = spawnattr_rd32((const char *)buf + BINPREFS_COUNT_OFFSET);
    uint32_t start = spawnattr_rd32((const char *)buf + BINPREFS_START_OFFSET);
    xpc_binprefs_t binprefs;
    uint32_t i;

    if (count == 0) {
        return NULL;
    }
    if ((limit - (size_t)start) < (size_t)count * 8u) {
        return NULL;
    }

    binprefs = xpc_binprefs_alloc();
    for (i = 0; i < count; i++) {
        const char *p = (const char *)buf + (size_t)start +
            SPAWNATTR_STRING_BASE + 4u + i * 8u;

        xpc_binprefs_add(binprefs, (int32_t)spawnattr_rd32(p - 4),
            (int32_t)spawnattr_rd32(p));
    }
    return binprefs;
}