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
 * xpc_peer_requirement.c — the peer-requirement type (xpc/peer_requirement.h).
 *
 * A peer requirement is an opaque object describing a code-signing property
 * the peer must satisfy.  This minimal build has no SecTask / code-signing
 * subsystem, so constructors validate their arguments and store the
 * requirement text; xpc_peer_requirement_match_received_message() performs a
 * structural match (the sender must have furnished a message with an audit
 * trailer) rather than a cryptographic one.
 */

#include "xpc_internal.h"

/* Satisfied only when the message carries a peer audit identity.  Without a
 * code-signing credential store there is nothing stronger to check; the
 * connection-side setters (xpc_connection_set_peer_*) are the same
 * store-and-validate surface. */
static bool
req_has_peer_identity(xpc_object_t message)
{
    if (!XPC_OBJECT_CHECK(message, &_xpc_type_dictionary)) return false;
    xpc_dictionary_t *d = (xpc_dictionary_t *)(void *)message;
    return d->has_audit_token;
}

struct _xpc_peer_requirement_s *
xpc_peer_requirement_alloc(xpc_peer_req_kind_t kind, char *text)
{
    struct _xpc_peer_requirement_s *r =
        (struct _xpc_peer_requirement_s *)(void *)xpc_object_alloc(
            &_xpc_type_peer_requirement, sizeof(*r));
    if (!r) { free(text); return NULL; }
    r->kind = (uint32_t)kind;
    r->text = text;
    return r;
}

static xpc_rich_error_t
oom_rich_error(void)
{
    return xpc_rich_error_create("out of memory", true);
}

xpc_peer_requirement_t
xpc_peer_requirement_create_entitlement_exists(const char *entitlement,
    xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!entitlement || !*entitlement) {
        if (error_out) {
            *error_out = (xpc_rich_error_t)(void *)
                xpc_rich_error_create("entitlement name required", false);
        }
        return NULL;
    }
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_ENTITLEMENT_EXISTS, strdup(entitlement));
    if (!r) {
        if (error_out) *error_out = oom_rich_error();
        return NULL;
    }
    return (xpc_peer_requirement_t)(void *)r;
}

xpc_peer_requirement_t
xpc_peer_requirement_create_entitlement_matches_value(const char *entitlement,
    xpc_object_t value, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!entitlement || !*entitlement || !value ||
        !(XPC_OBJECT_CHECK(value, &_xpc_type_string) ||
            XPC_OBJECT_CHECK(value, &_xpc_type_bool) ||
            XPC_OBJECT_CHECK(value, &_xpc_type_int64) ||
            XPC_OBJECT_CHECK(value, &_xpc_type_uint64))) {
        if (error_out) {
            *error_out = (xpc_rich_error_t)(void *)
                xpc_rich_error_create("unsupported entitlement value", false);
        }
        return NULL;
    }
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_ENTITLEMENT_VALUE, strdup(entitlement));
    if (!r) {
        if (error_out) *error_out = oom_rich_error();
        return NULL;
    }
    return (xpc_peer_requirement_t)(void *)r;
}

xpc_peer_requirement_t
xpc_peer_requirement_create_team_identity(const char *signing_identifier,
    xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_TEAM_IDENTITY,
        signing_identifier ? strdup(signing_identifier) : NULL);
    if (!r) {
        if (error_out) *error_out = oom_rich_error();
        return NULL;
    }
    return (xpc_peer_requirement_t)(void *)r;
}

xpc_peer_requirement_t
xpc_peer_requirement_create_platform_identity(const char *signing_identifier,
    xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_PLATFORM_IDENTITY,
        signing_identifier ? strdup(signing_identifier) : NULL);
    if (!r) {
        if (error_out) *error_out = oom_rich_error();
        return NULL;
    }
    return (xpc_peer_requirement_t)(void *)r;
}

xpc_peer_requirement_t
xpc_peer_requirement_create_lwcr(xpc_object_t lwcr, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!lwcr) {
        if (error_out) {
            *error_out = (xpc_rich_error_t)(void *)
                xpc_rich_error_create(
                    "lightweight code requirement required", false);
        }
        return NULL;
    }
    /* The lightweight-code-requirement wire kind is 0x1500/d; store its
     * textual description as the requirement text. */
    char *text = xpc_description_create(lwcr);
    struct _xpc_peer_requirement_s *r = xpc_peer_requirement_alloc(
        XPC_PEER_REQ_LWCR, text);
    if (!r) {
        if (error_out) *error_out = oom_rich_error();
        return NULL;
    }
    return (xpc_peer_requirement_t)(void *)r;
}

bool
xpc_peer_requirement_match_received_message(xpc_peer_requirement_t req,
    xpc_object_t message, xpc_rich_error_t *error_out)
{
    if (error_out) *error_out = NULL;
    if (!req || !XPC_OBJECT_CHECK(req, &_xpc_type_peer_requirement)) {
        if (error_out) {
            *error_out = (xpc_rich_error_t)(void *)
                xpc_rich_error_create("invalid peer requirement", false);
        }
        return false;
    }
    if (!XPC_OBJECT_CHECK(message, &_xpc_type_dictionary)) {
        if (error_out) {
            *error_out = (xpc_rich_error_t)(void *)
                xpc_rich_error_create(
                    "message does not carry a sender identity", false);
        }
        return false;
    }
    /* Structural check: a received request with an audit trailer is a peer
     * we can reason about.  (Full code-signing validation arrives with the
     * SecTask bridge.) */
    return req_has_peer_identity(message);
}