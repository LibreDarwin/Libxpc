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
 * xpc.h — public API for the reimplemented XPC framework.
 *
 * Drop-in compatible subset of Apple's libxpc public interface,
 * covering the core object model, value types, and containers.
 * Wire serialization / pipe layer lives in the private header.
 *
 * This is an independent reimplementation; it is NOT Apple's code.
 * The byte-level wire format it speaks is documented in
 * docs/GUIDE.md (reverse-engineered and cross-validated
 * against the real libxpc on macOS 26).
 */

#ifndef __XPC_XPC_H__
#define __XPC_XPC_H__

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <uuid/uuid.h>
#include <mach/mach.h>
#if __has_include(<bsm/audit.h>)
#include <bsm/audit.h>
#endif
#if __has_include(<bsm/libbsm.h>)
#include <bsm/libbsm.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Types
/*!
 * @typedef xpc_object_t
 * @abstract First-class XPC objects.
 */
typedef struct _xpc_object_s *xpc_object_t;


/*!
 * @typedef xpc_type_t
 * @abstract The type of an XPC object.
 */
typedef const struct _xpc_type_s *xpc_type_t;

#define XPC_ARRAY_APPEND ((size_t)-1)

/*!
 * @typedef xpc_handler_t
 * @abstract Call block with an object.
 */
typedef void (^xpc_handler_t)(xpc_object_t object);
typedef void (*xpc_finalizer_t)(void *context);

/* Object typedefs matching the public SDK headers.  Each is an opaque
 * pointer to an internal struct tag; the structural definitions live in
 * xpc_internal.h (public consumers see only the incomplete tag). */
typedef struct _xpc_endpoint_s *xpc_endpoint_t;
typedef struct _xpc_connection_s *xpc_connection_t;
typedef struct _xpc_activity_s *xpc_activity_t;
typedef struct _xpc_session_s *xpc_session_t;
typedef struct _xpc_listener_s *xpc_listener_t;
typedef struct _xpc_rich_error_s *xpc_rich_error_t;
typedef struct _xpc_peer_requirement_s *xpc_peer_requirement_t;

/* libdispatch is not linked into this hermetic xpc, but the SDK headers
 * are on the include path for consumers: use the real dispatch types when
 * they are available (avoiding a typedef clash), otherwise fall back to
 * opaque declarations.  Queues are stored opaquely by the connection layer
 * and blocks are delivered on a per-connection receive thread. */
#if __has_include(<dispatch/dispatch.h>)
#include <dispatch/dispatch.h>
#else
typedef void *dispatch_queue_t;
typedef void (^dispatch_block_t)(void);
#endif

#pragma mark - XPC object lifecycle

xpc_type_t xpc_get_type(xpc_object_t object);

bool xpc_equal(xpc_object_t object1, xpc_object_t object2);
uint64_t xpc_hash(xpc_object_t object);

char *xpc_copy_description(xpc_object_t object);

xpc_object_t xpc_retain(xpc_object_t object);
void xpc_release(xpc_object_t object);

#pragma mark - XPC types

extern const struct _xpc_type_s _xpc_type_null;
extern const struct _xpc_type_s _xpc_type_bool;
extern const struct _xpc_type_s _xpc_type_int64;
extern const struct _xpc_type_s _xpc_type_uint64;
extern const struct _xpc_type_s _xpc_type_double;
extern const struct _xpc_type_s _xpc_type_date;
extern const struct _xpc_type_s _xpc_type_data;
extern const struct _xpc_type_s _xpc_type_string;
extern const struct _xpc_type_s _xpc_type_uuid;
extern const struct _xpc_type_s _xpc_type_array;
extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s _xpc_type_error;
extern const struct _xpc_type_s _xpc_type_connection;
extern const struct _xpc_type_s _xpc_type_endpoint;
extern const struct _xpc_type_s _xpc_type_activity;
extern const struct _xpc_type_s _xpc_type_session;
extern const struct _xpc_type_s _xpc_type_listener;
extern const struct _xpc_type_s _xpc_type_fd;
extern const struct _xpc_type_s _xpc_type_rich_error;
extern const struct _xpc_type_s _xpc_type_mach_recv;
extern const struct _xpc_type_s _xpc_type_peer_requirement;

#pragma mark - Connection

extern const struct _xpc_dictionary_s _xpc_error_connection_interrupted;
extern const struct _xpc_dictionary_s _xpc_error_connection_invalid;
extern const struct _xpc_dictionary_s _xpc_error_termination_imminent;
extern const struct _xpc_dictionary_s _xpc_error_peer_code_signing_requirement;

#define XPC_ERROR_CONNECTION_INTERRUPTED \
    ((xpc_object_t)&_xpc_error_connection_interrupted)
#define XPC_ERROR_CONNECTION_INVALID \
    ((xpc_object_t)&_xpc_error_connection_invalid)
#define XPC_ERROR_TERMINATION_IMMINENT \
    ((xpc_object_t)&_xpc_error_termination_imminent)
#define XPC_ERROR_PEER_CODE_SIGNING_REQUIREMENT \
    ((xpc_object_t)&_xpc_error_peer_code_signing_requirement)

#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1 << 0)
#define XPC_CONNECTION_MACH_SERVICE_PRIVILEGED (1 << 1)
#define XPC_CONNECTION_MACH_SERVICE_ANONYMOUS (1 << 2)

typedef uint64_t xpc_session_create_flags_t;

#define XPC_SESSION_CREATE_NONE (0)
#define XPC_SESSION_CREATE_INACTIVE (1 << 0)
#define XPC_SESSION_CREATE_MACH_PRIVILEGED (1 << 1)

xpc_connection_t xpc_connection_create(const char *name,
    dispatch_queue_t targetq);
xpc_connection_t xpc_connection_create_mach_service(const char *name,
    dispatch_queue_t targetq, uint64_t flags);
xpc_connection_t xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint);
void xpc_connection_set_target_queue(xpc_connection_t connection,
    dispatch_queue_t targetq);
void xpc_connection_set_event_handler(xpc_connection_t connection,
    xpc_handler_t handler);
void xpc_connection_activate(xpc_connection_t connection);
void xpc_connection_suspend(xpc_connection_t connection);
void xpc_connection_resume(xpc_connection_t connection);
void xpc_connection_send_message(xpc_connection_t connection,
    xpc_object_t message);
void xpc_connection_send_barrier(xpc_connection_t connection,
    dispatch_block_t barrier);
void xpc_connection_send_message_with_reply(xpc_connection_t connection,
    xpc_object_t message, dispatch_queue_t replyq, xpc_handler_t handler);
xpc_object_t xpc_connection_send_message_with_reply_sync(
    xpc_connection_t connection, xpc_object_t message);
void xpc_connection_cancel(xpc_connection_t connection);
const char *xpc_connection_get_name(xpc_connection_t connection);
uid_t xpc_connection_get_euid(xpc_connection_t connection);
gid_t xpc_connection_get_egid(xpc_connection_t connection);
pid_t xpc_connection_get_pid(xpc_connection_t connection);
au_asid_t xpc_connection_get_asid(xpc_connection_t connection);
void xpc_connection_set_context(xpc_connection_t connection,
    void *context);
void *xpc_connection_get_context(xpc_connection_t connection);
void xpc_connection_set_finalizer_f(xpc_connection_t connection,
    xpc_finalizer_t finalizer);
int xpc_connection_set_peer_code_signing_requirement(
    xpc_connection_t connection, const char *requirement);
int xpc_connection_set_peer_entitlement_exists_requirement(
    xpc_connection_t connection, const char *entitlement);
int xpc_connection_set_peer_entitlement_matches_value_requirement(
    xpc_connection_t connection, const char *entitlement, xpc_object_t value);
int xpc_connection_set_peer_team_identity_requirement(
    xpc_connection_t connection, const char *signing_identifier);
int xpc_connection_set_peer_platform_identity_requirement(
    xpc_connection_t connection, const char *signing_identifier);
int xpc_connection_set_peer_lightweight_code_requirement(
    xpc_connection_t connection, xpc_object_t lwcr);
void xpc_connection_set_peer_requirement(xpc_connection_t connection,
    xpc_peer_requirement_t peer_requirement);
char *xpc_connection_copy_invalidation_reason(xpc_connection_t connection);

/* Internal: port-based construction (the flat header's pre-Slice-C form),
 * kept for the endpoint path and same-task wiring. */
xpc_connection_t xpc_connection_create_with_port(mach_port_t port,
    xpc_handler_t handler, void *context, xpc_finalizer_t finalizer);
void xpc_connection_register_mach_service(const char *name, mach_port_t port);
mach_port_t xpc_connection_get_port(xpc_object_t connection);

#pragma mark - Rich error

xpc_rich_error_t xpc_rich_error_create(const char *desc, bool can_retry);

int xpc_rich_error_can_retry(xpc_object_t error);
char *xpc_rich_error_copy_description(xpc_object_t error);

#pragma mark - Peer requirement

xpc_peer_requirement_t xpc_peer_requirement_create_entitlement_exists(
    const char *entitlement, xpc_rich_error_t *error_out);
xpc_peer_requirement_t xpc_peer_requirement_create_entitlement_matches_value(
    const char *entitlement, xpc_object_t value, xpc_rich_error_t *error_out);
xpc_peer_requirement_t xpc_peer_requirement_create_team_identity(
    const char *signing_identifier, xpc_rich_error_t *error_out);
xpc_peer_requirement_t xpc_peer_requirement_create_platform_identity(
    const char *signing_identifier, xpc_rich_error_t *error_out);
xpc_peer_requirement_t xpc_peer_requirement_create_lwcr(xpc_object_t lwcr,
    xpc_rich_error_t *error_out);
bool xpc_peer_requirement_match_received_message(
    xpc_peer_requirement_t req, xpc_object_t message,
    xpc_rich_error_t *error_out);

#pragma mark - Endpoint

xpc_object_t xpc_endpoint_create(mach_port_t port);
mach_port_t xpc_endpoint_get_port(xpc_object_t endpoint);
xpc_object_t xpc_endpoint_copy_listener_port(xpc_object_t endpoint);

#pragma mark - Activity

typedef void (^xpc_activity_handler_t)(xpc_activity_t activity);
typedef void (^xpc_activity_eligibility_changed_handler_t)(
    xpc_activity_t activity);

/* Dictionary keys for the XPC activity criteria dictionary. */
#define XPC_ACTIVITY_INTERVAL "XPC_ACTIVITY_INTERVAL"
#define XPC_ACTIVITY_REPEATING "XPC_ACTIVITY_REPEATING"
#define XPC_ACTIVITY_DELAY "XPC_ACTIVITY_DELAY"
#define XPC_ACTIVITY_GRACE_PERIOD "XPC_ACTIVITY_GRACE_PERIOD"

/* Predefined interval constants (seconds). */
#define XPC_ACTIVITY_INTERVAL_1_MIN (1 * 60)
#define XPC_ACTIVITY_INTERVAL_5_MIN (5 * 60)
#define XPC_ACTIVITY_INTERVAL_15_MIN (15 * 60)
#define XPC_ACTIVITY_INTERVAL_30_MIN (30 * 60)
#define XPC_ACTIVITY_INTERVAL_1_HOUR (60 * 60)
#define XPC_ACTIVITY_INTERVAL_4_HOURS (4 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_8_HOURS (8 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_1_DAY (24 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_7_DAYS (7 * 24 * 60 * 60)

#define XPC_ACTIVITY_PRIORITY "XPC_ACTIVITY_PRIORITY"
#define XPC_ACTIVITY_PRIORITY_MAINTENANCE "XPC_ACTIVITY_PRIORITY_MAINTENANCE"
#define XPC_ACTIVITY_PRIORITY_UTILITY "XPC_ACTIVITY_PRIORITY_UTILITY"
#define XPC_ACTIVITY_ALLOW_BATTERY "XPC_ACTIVITY_ALLOW_BATTERY"
#define XPC_ACTIVITY_REQUIRE_SCREEN_SLEEP "XPC_ACTIVITY_REQUIRE_SCREEN_SLEEP" /* bool */
#define XPC_ACTIVITY_PREVENT_DEVICE_SLEEP "XPC_ACTIVITY_PREVENT_DEVICE_SLEEP" /* bool */

/* Pass this as the criteria to xpc_activity_register to check in with an
 * existing activity instead of installing fresh criteria. */
extern const xpc_object_t XPC_ACTIVITY_CHECK_IN;

enum {
    XPC_ACTIVITY_STATE_CHECK_IN,
    XPC_ACTIVITY_STATE_WAIT,
    XPC_ACTIVITY_STATE_RUN,
    XPC_ACTIVITY_STATE_DEFER,
    XPC_ACTIVITY_STATE_CONTINUE,
    XPC_ACTIVITY_STATE_DONE,
};
typedef long xpc_activity_state_t;

void xpc_activity_register(const char *identifier, xpc_object_t criteria,
    xpc_activity_handler_t handler);
void xpc_activity_unregister(const char *identifier);
xpc_object_t xpc_activity_copy_criteria(xpc_activity_t activity);
void xpc_activity_set_criteria(xpc_activity_t activity, xpc_object_t criteria);
xpc_activity_state_t xpc_activity_get_state(xpc_activity_t activity);
bool xpc_activity_set_state(xpc_activity_t activity, xpc_activity_state_t state);
bool xpc_activity_set_state_with_completion_status(xpc_activity_t activity,
    xpc_activity_state_t state, long status);
bool xpc_activity_set_completion_status(xpc_activity_t activity, long status);
bool xpc_activity_should_defer(xpc_activity_t activity);
void xpc_activity_should_be_data_budgeted(xpc_activity_t activity,
    bool budgeted);
bool xpc_activity_defer_until_percentage(xpc_activity_t activity,
    long percentage);
bool xpc_activity_defer_until_network_change(xpc_activity_t activity);
long xpc_activity_get_percentage(xpc_activity_t activity);
void xpc_activity_set_network_threshold(xpc_activity_t activity,
    long percentage);
char *xpc_activity_copy_identifier(xpc_activity_t activity);
dispatch_queue_t xpc_activity_copy_dispatch_queue(xpc_activity_t activity);
void xpc_activity_add_eligibility_changed_handler(xpc_activity_t activity,
    xpc_activity_eligibility_changed_handler_t handler);
void xpc_activity_remove_eligibility_changed_handler(xpc_activity_t activity,
    xpc_activity_eligibility_changed_handler_t handler);

#pragma mark - Session

typedef void (^xpc_session_incoming_message_handler_t)(xpc_object_t message);
typedef void (^xpc_session_cancel_handler_t)(xpc_rich_error_t error);
typedef void (^xpc_session_reply_handler_t)(xpc_object_t reply,
    xpc_rich_error_t error);

xpc_session_t xpc_session_create_xpc_service(const char *name,
    dispatch_queue_t target_queue, xpc_session_create_flags_t flags,
    xpc_rich_error_t *error_out);
xpc_session_t xpc_session_create_mach_service(const char *mach_service,
    dispatch_queue_t target_queue, xpc_session_create_flags_t flags,
    xpc_rich_error_t *error_out);
void xpc_session_set_incoming_message_handler(xpc_session_t session,
    xpc_session_incoming_message_handler_t handler);
void xpc_session_set_cancel_handler(xpc_session_t session,
    xpc_session_cancel_handler_t cancel_handler);
void xpc_session_set_target_queue(xpc_session_t session,
    dispatch_queue_t target_queue);
bool xpc_session_activate(xpc_session_t session,
    xpc_rich_error_t *error_out);
void xpc_session_cancel(xpc_session_t session);
char *xpc_session_copy_description(xpc_session_t session);
xpc_rich_error_t xpc_session_send_message(xpc_session_t session,
    xpc_object_t message);
xpc_object_t xpc_session_send_message_with_reply_sync(xpc_session_t session,
    xpc_object_t message, xpc_rich_error_t *error_out);
void xpc_session_send_message_with_reply_async(xpc_session_t session,
    xpc_object_t message, xpc_session_reply_handler_t reply_handler);
int xpc_session_set_peer_code_signing_requirement(xpc_session_t session,
    const char *requirement);
void xpc_session_set_peer_requirement(xpc_session_t session,
    xpc_peer_requirement_t requirement);

#pragma mark - Listener

/* The listener is the service-side counterpart of the session: it owns a
 * named listening connection and mints one incoming peer session per client
 * connection.  In this hermetic transport the listener registers its name at
 * activate() so clients resolve it with xpc_connection_create_mach_service(),
 * and the peer session is a manageability wrapper over the (single) incoming
 * client connection.  All message traffic flows over mach port 0 (see
 * local/Libxpc.md), so there is no per-peer port handoff. */

typedef uint64_t xpc_listener_create_flags_t;

#define XPC_LISTENER_CREATE_NONE (0)
#define XPC_LISTENER_CREATE_INACTIVE (1 << 0)
#define XPC_LISTENER_CREATE_FORCE_MACH (1 << 1)
#define XPC_LISTENER_CREATE_FORCE_XPCSERVICE (1 << 2)

typedef void (^xpc_listener_incoming_session_handler_t)(xpc_session_t peer);

xpc_listener_t xpc_listener_create(const char *service,
    dispatch_queue_t target_queue, xpc_listener_create_flags_t flags,
    xpc_listener_incoming_session_handler_t incoming_session_handler,
    xpc_rich_error_t *error_out);
bool xpc_listener_activate(xpc_listener_t listener,
    xpc_rich_error_t *error_out);
void xpc_listener_cancel(xpc_listener_t listener);
void xpc_listener_reject_peer(xpc_session_t peer, const char *reason);
int xpc_listener_set_peer_code_signing_requirement(xpc_listener_t listener,
    const char *requirement);
void xpc_listener_set_peer_requirement(xpc_listener_t listener,
    xpc_peer_requirement_t requirement);
char *xpc_listener_copy_description(xpc_listener_t listener);
xpc_listener_t xpc_listener_create_anonymous(void);
xpc_endpoint_t xpc_listener_create_endpoint(xpc_listener_t listener);
void xpc_listener_set_incoming_session_handler(xpc_listener_t listener,
    xpc_listener_incoming_session_handler_t handler);

#pragma mark - Null

xpc_object_t xpc_null_create(void);

#pragma mark - Boolean

xpc_object_t xpc_bool_create(bool value);
bool xpc_bool_get_value(xpc_object_t object);

#pragma mark - Signed integer

xpc_object_t xpc_int64_create(int64_t value);
int64_t xpc_int64_get_value(xpc_object_t object);

#pragma mark - Unsigned integer

xpc_object_t xpc_uint64_create(uint64_t value);
uint64_t xpc_uint64_get_value(xpc_object_t object);

#pragma mark - Double

xpc_object_t xpc_double_create(double value);
double xpc_double_get_value(xpc_object_t object);

#pragma mark - Date

xpc_object_t xpc_date_create(int64_t interval);
xpc_object_t xpc_date_create_from_timespec(struct timespec *ts);
int64_t xpc_date_get_value(xpc_object_t object);
void xpc_date_get_timespec(xpc_object_t object, struct timespec *ts);

#pragma mark - Data

xpc_object_t xpc_data_create(const void *bytes, size_t length);
xpc_object_t xpc_data_create_with_bytes(const void *bytes, size_t length);
size_t xpc_data_get_length(xpc_object_t object);
const void *xpc_data_get_bytes_ptr(xpc_object_t object);
size_t xpc_data_get_bytes(xpc_object_t object, void *bytes, size_t offset,
    size_t length);

#pragma mark - String

xpc_object_t xpc_string_create(const char *string);
xpc_object_t xpc_string_create_with_format(const char *fmt, ...);
xpc_object_t xpc_string_create_with_length(const char *string, size_t length);
size_t xpc_string_get_length(xpc_object_t object);
const char *xpc_string_get_string_ptr(xpc_object_t object);

#pragma mark - UUID

xpc_object_t xpc_uuid_create(const uuid_t uuid);
const uint8_t *xpc_uuid_get_bytes(xpc_object_t object);

#pragma mark - File descriptors

xpc_object_t xpc_fd_create(int fd);
int xpc_fd_dup(xpc_object_t xfd);

#pragma mark - Array

xpc_object_t xpc_array_create(const xpc_object_t *objects,
    size_t count);
xpc_object_t xpc_array_create_np(const xpc_object_t *objects,
    size_t count);
size_t xpc_array_get_count(xpc_object_t object);
void xpc_array_set_value(xpc_object_t object, size_t index,
    xpc_object_t value);
void xpc_array_append_value(xpc_object_t object, xpc_object_t value);
xpc_object_t xpc_array_get_value(xpc_object_t object, size_t index);
bool xpc_array_get_bool(xpc_object_t object, size_t index);
int64_t xpc_array_get_int64(xpc_object_t object, size_t index);
uint64_t xpc_array_get_uint64(xpc_object_t object, size_t index);
double xpc_array_get_double(xpc_object_t object, size_t index);
const char *xpc_array_get_string(xpc_object_t object, size_t index);
const void *xpc_array_get_data(xpc_object_t object, size_t index,
    size_t *length);
bool xpc_array_get_data_np(xpc_object_t object, size_t index,
    const void **bytes, size_t *length);
const uint8_t *xpc_array_get_uuid(xpc_object_t object, size_t index);
int64_t xpc_array_get_date(xpc_object_t object, size_t index);
bool xpc_array_apply(xpc_object_t object,
    bool (^applier)(size_t index, xpc_object_t value));
void xpc_array_set_fd(xpc_object_t object, size_t index, int fd);
int xpc_array_dup_fd(xpc_object_t object, size_t index);
void xpc_array_set_connection(xpc_object_t object, size_t index,
    xpc_object_t connection);
xpc_object_t xpc_array_create_connection(xpc_object_t object,
    size_t index);

#pragma mark - Dictionary

xpc_object_t xpc_dictionary_create(const char *const *keys,
    const xpc_object_t *values, size_t count);
size_t xpc_dictionary_get_count(xpc_object_t object);
void xpc_dictionary_set_value(xpc_object_t object, const char *key,
    xpc_object_t value);
xpc_object_t xpc_dictionary_get_value(xpc_object_t object,
    const char *key);
void xpc_dictionary_remove_value(xpc_object_t object, const char *key);
bool xpc_dictionary_get_bool(xpc_object_t object, const char *key);
int64_t xpc_dictionary_get_int64(xpc_object_t object, const char *key);
uint64_t xpc_dictionary_get_uint64(xpc_object_t object, const char *key);
double xpc_dictionary_get_double(xpc_object_t object, const char *key);
const char *xpc_dictionary_get_string(xpc_object_t object,
    const char *key);
const void *xpc_dictionary_get_data(xpc_object_t object, const char *key,
    size_t *length);
bool xpc_dictionary_get_data_np(xpc_object_t object, const char *key,
    const void **bytes, size_t *length);
const uint8_t *xpc_dictionary_get_uuid(xpc_object_t object,
    const char *key);
xpc_object_t xpc_dictionary_get_date(xpc_object_t object,
    const char *key);
bool xpc_dictionary_apply(xpc_object_t object,
    bool (^applier)(const char *key, xpc_object_t value));
void xpc_dictionary_set_bool(xpc_object_t object, const char *key,
    bool value);
void xpc_dictionary_set_int64(xpc_object_t object, const char *key,
    int64_t value);
void xpc_dictionary_set_uint64(xpc_object_t object, const char *key,
    uint64_t value);
void xpc_dictionary_set_double(xpc_object_t object, const char *key,
    double value);
void xpc_dictionary_set_string(xpc_object_t object, const char *key,
    const char *string);
void xpc_dictionary_set_data(xpc_object_t object, const char *key,
    const void *bytes, size_t length);
void xpc_dictionary_set_uuid(xpc_object_t object, const char *key,
    const uuid_t uuid);
void xpc_dictionary_set_date(xpc_object_t object, const char *key,
    int64_t value);
void xpc_dictionary_set_mach_send(xpc_object_t object, const char *key,
    mach_port_t port);
void xpc_dictionary_set_fd(xpc_object_t object, const char *key, int fd);
int xpc_dictionary_dup_fd(xpc_object_t object, const char *key);
void xpc_dictionary_set_connection(xpc_object_t object, const char *key,
    xpc_object_t connection);
xpc_object_t xpc_dictionary_create_connection(xpc_object_t object,
    const char *key);
xpc_object_t xpc_dictionary_create_reply(xpc_object_t original);
xpc_object_t xpc_dictionary_get_remote_connection(xpc_object_t object);

#pragma mark - Reply context

/*
 * Reply machinery for dictionaries received over a pipe (see the pipe-layer
 * note in xpc_internal.h).  A received request carries a reply capability;
 * create_reply() mints a fresh reply dictionary that owns it, and
 * send_reply() ships one to the requester.  The capability moves exactly
 * once, so these are destructive.  handoff_reply(_f) transfer it to a new
 * dictionary (the _f variant runs a finalizer when the handoff is later
 * dropped without being sent).
 */
bool xpc_dictionary_expects_reply(xpc_object_t original);
void xpc_dictionary_send_reply(xpc_object_t reply);
xpc_object_t xpc_dictionary_handoff_reply(xpc_object_t reply);
xpc_object_t xpc_dictionary_handoff_reply_f(xpc_object_t reply,
    void (*finalizer)(void *context), void *context);

#pragma mark - Mach-Recv

/*
 * Wrap a receive right for transport as a dictionary value.  The object
 * owns the right from birth (as in Apple's xpc_mach_recv_create(3));
 * serializing the value moves the right to the peer (MOVE_RECEIVE), and
 * xpc_mach_recv_extract_right() returns it to a caller -- each either
 * consumes the right or releases it.
 */
xpc_object_t xpc_mach_recv_create(mach_port_t port);
mach_port_t xpc_mach_recv_extract_right(xpc_object_t object);
void xpc_dictionary_set_mach_recv(xpc_object_t object, const char *key,
    mach_port_t port);

#pragma mark - Mach-Send

/*
 * Wrap a send right for transport.  The object borrows the right; the
 * caller keeps ownership and remains responsible for it.  Mirrors
 * Apple's xpc_mach_send_create(3).
 */
xpc_object_t xpc_mach_send_create(mach_port_t port);
mach_port_t xpc_mach_send_get_port(xpc_object_t object);

#ifdef __cplusplus
}
#endif

#endif /* __XPC_XPC_H__ */
