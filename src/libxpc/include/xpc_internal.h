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
 * xpc_internal.h — private internals of the reimplemented XPC framework.
 *
 * Object model:
 *   Every xpc_object_t points to a struct _xpc_object_s whose first two
 *   words are (isa, refs).  `isa` points to one of the _xpc_type_*
 *   singletons in xpc_types.c; `refs` is an atomic refcount.
 *
 * Wire format:
 *   Serialization follows docs/GUIDE.md exactly — the CPX@
 *   envelope, the 4-byte aligned keys, and the type tags below.
 *   Numbers are little-endian on all supported platforms.
 */

#ifndef __XPC_INTERNAL_H__
#define __XPC_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include <mach/mach.h>
#include "xpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Object model

/* Kind of object, mirrors the public type singletons. */
typedef enum xpc_kind {
    XPC_KIND_NULL = 0,
    XPC_KIND_BOOL,
    XPC_KIND_INT64,
    XPC_KIND_UINT64,
    XPC_KIND_DOUBLE,
    XPC_KIND_DATE,
    XPC_KIND_DATA,
    XPC_KIND_STRING,
    XPC_KIND_UUID,
    XPC_KIND_MACH_SEND,
    XPC_KIND_SHMEM,
    XPC_KIND_ARRAY,
    XPC_KIND_DICTIONARY,
    XPC_KIND_ERROR,
    XPC_KIND_CONNECTION,
    XPC_KIND_ENDPOINT,
    XPC_KIND_ACTIVITY,
    XPC_KIND_SESSION,
    XPC_KIND_LISTENER,
    XPC_KIND_FD,
    XPC_KIND_RICH_ERROR,
    XPC_KIND_MACH_RECV,
    XPC_KIND_PEER_REQUIREMENT,
    XPC_KIND_COUNT,
} xpc_kind_t;

/* Type descriptor: named singletons per XPC kind. */
struct _xpc_type_s {
    const char *name;       /* e.g. "null", "int64" — used by description */
    xpc_kind_t kind;
};

typedef bool (*xpc_array_applier_t)(size_t index, xpc_object_t value);
typedef bool (*xpc_dictionary_applier_t)(const char *key, xpc_object_t value);
typedef void (*xpc_dictionary_applier_f_t)(const char *key,
    xpc_object_t value, void *context);

struct _xpc_object_s {
    xpc_type_t isa;             /* one of the _xpc_type_* singletons */
    _Atomic(uint64_t) refs;     /* atomic retain count */
};

/* Every concrete object type embeds this header first. */
#define XPC_OBJECT_HEADER(kindptr) \
    { .isa = (kindptr), .refs = 1 }

/* Downcast helper — behaves like Apple's _xpc_object_cast(). */
#define XPC_CAST(type, obj) \
    ((type *)(void *)(obj))
#define XPC_OBJECT_CHECK(obj, typeptr) \
    (obj && ((xpc_object_t)(obj))->isa == (typeptr))

/* Internal storage layouts (public bits expose a subset of these). */

typedef struct _xpc_scalar_s {
    struct _xpc_object_s hdr;
    union {
        bool bval;
        int64_t i64;
        uint64_t u64;
        double dbl;
        int64_t date_ns;
    } v;
} xpc_scalar_t;

typedef struct _xpc_string_s {
    struct _xpc_object_s hdr;
    char *data;         /* NUL-terminated */
    size_t length;      /* strlen(data) */
} xpc_string_t;

typedef struct _xpc_data_s {
    struct _xpc_object_s hdr;
    uint8_t *data;
    size_t length;
} xpc_data_t;

typedef struct _xpc_uuid_s {
    struct _xpc_object_s hdr;
    uuid_t uuid;
} xpc_uuid_t;

typedef struct _xpc_mach_send_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* send right */
    bool dispose;           /* true: release deallocates the right
                             * (rights received from the wire) */
} xpc_mach_send_t;

typedef struct _xpc_mach_recv_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* receive right; NULL once consumed */
    bool dispose;           /* true: release destroys an unconsumed right */
} xpc_mach_recv_t;

typedef struct _xpc_shmem_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* memory-entry send right */
    uint64_t size;          /* page-aligned span of the entry */
    bool dispose;           /* true: release deallocates the right */
    void *origin;           /* same-task origin region, or NULL (the kernel
                             * refuses to re-map a task's own memory entry,
                             * so for locally-created entries the origin IS
                             * the mapping) */
} xpc_shmem_t;

typedef struct _xpc_array_s {
    struct _xpc_object_s hdr;
    xpc_object_t *items;
    size_t count;
    size_t capacity;
} xpc_array_t;

typedef struct _xpc_dictionary_s {
    struct _xpc_object_s hdr;
    char **keys;
    xpc_object_t *values;
    size_t count;
    size_t capacity;
    audit_token_t audit_token;  /* sender token, set on receipt (§xpc_routines) */
    bool has_audit_token;
    uint8_t msg_mode;           /* reply context: 0 none, 1 received request,
                                 * 2 reply (the state that lets
                                 * xpc_dictionary_create_reply()/send_reply()
                                 * work).  Not serialized — it is envelope
                                 * context, not dict content. */
    mach_port_t reply_port;     /* reply capability: the send(-once) right a
                                 * request arrived on.  Consumed (moved) when
                                 * a reply is sent. */
    uint8_t reply_disposition;  /* the received message's local-bit disposition
                                 * (send-once vs send); reused as the reply's
                                 * remote disposition so the right kind moves
                                 * correctly. */
    void (*reply_finalizer)(void *context); /* handoff_reply_f(): invoked when
                                             * this dict is released */
    void *reply_finalizer_ctx;
} xpc_dictionary_t;

typedef struct _xpc_error_s {
    struct _xpc_object_s hdr;
    char *desc;         /* human-readable failure message */
    int code;
} xpc_error_t;

struct _xpc_endpoint_s {
    struct _xpc_object_s hdr;
    mach_port_t port;
};

struct _xpc_connection_s {
    struct _xpc_object_s hdr;

    /* Identity. */
    char *name;                     /* service / mach-service / anonymous name */
    bool listener;                  /* created with
                                     * XPC_CONNECTION_MACH_SERVICE_LISTENER */
    bool connected;                 /* a peer send right is established */
    bool is_anonymous;              /* XPC_CONNECTION_MACH_SERVICE_ANONYMOUS */

    /* Peer (send) right and our local receive right.  The receive right is
     * owned by the connection and destroyed at teardown.  self_port is a
     * send-right alias (same task) of recv_right: the name a peer uses to
     * reach us, and what xpc_dictionary/array_set_connection advertise. */
    mach_port_t peer_port;          /* send right to the peer, or MACH_PORT_NULL */
    mach_port_t recv_right;         /* our receive right, or MACH_PORT_NULL for
                                     * a pure send-capability partner */
    mach_port_t self_port;          /* MAKE_SEND alias of recv_right, or NULL */

    /* State machine. */
    int32_t state;                  /* XPC_CONN_STATE_* */
    _Atomic(int32_t) resume_count;  /* >0: events flow to the handler */
    bool cancelled;
    bool has_handler;

    /* Event delivery.  The handler is a copied block; target_queue is
     * stored opaquely (no libdispatch in this build — a daemon-owned
     * receive thread serializes delivery instead). */
    xpc_handler_t handler;
    xpc_finalizer_t finalizer;
    void *context;
    void *target_queue;             /* dispatch_queue_t, stored only */

    /* Peer identity, captured from the first received trailer. */
    bool have_peer_audit;
    audit_token_t peer_audit;

    /* Per-connection serialization: protects state, resume_count, and the
     * receive thread's shutdown flag.  Messages received while suspended
     * queue in the kernel on recv_right and are delivered on the next
     * resume, so no user-space inbound queue is needed. */
    pthread_mutex_t lock;
    pthread_cond_t cond;

    /* Receive thread driving the event handler. */
    pthread_t rx_thread;
    bool rx_thread_started;
    bool shutdown;

    /* Invocation of the event handler. */
    xpc_object_t inbound_error;     /* XPC_ERROR_* to deliver, or NULL */

    /* Stored peer requirements (xpc_connection_set_peer_*_requirement). */
    char *peer_code_signing_requirement;
    xpc_peer_requirement_t peer_requirement;
    bool invalidate_on_requirement_failure;

    /* copy_invalidation_reason. */
    char *invalidation_reason;

    /* Messages sent before the peer send right is established.  Flushed at
     * connect; each entry is retained until flushed.
     * Outstanding async-reply receive ports, one per in-flight
     * xpc_connection_send_message_with_reply(), are destroyed at cancel to
     * unblock their waiter threads.  Both arrays are guarded by lock. */
    xpc_object_t *pending;
    size_t pending_count, pending_cap;
    mach_port_t *reply_ports;
    size_t reply_count, reply_cap;
};

struct _xpc_session_s {
    struct _xpc_object_s hdr;
    xpc_connection_t connection;    /* backing connection, retained */
    bool activated;
    bool cancelled;
    /* Session-standard handlers, copied when set.  At activate() they are
     * adapted onto the backing connection: message events reach
     * incoming_handler, error/lifecycle events reach cancel_handler as a
     * rich error. */
    xpc_session_incoming_message_handler_t incoming_handler;
    xpc_session_cancel_handler_t cancel_handler;
    /* Non-owning back pointer to the listener that minted this peer, valid
     * while the listener owns this session in its single peer slot.  Used by
     * xpc_listener_reject_peer(); the listener clears it before dropping the
     * peer so a stale reject no-ops.  Never retained: that would cycle with
     * the listener's peer reference. */
    struct _xpc_listener_s *owner;
};

struct _xpc_listener_s {
    struct _xpc_object_s hdr;
    xpc_connection_t connection;    /* backing listening connection, transferred ref */
    char *name;                     /* service name; NULL for anonymous */
    /* The incoming-session handler, copied when set.  A client's first
     * message mints an incoming peer session handed to this handler;
     * subsequent messages route to the peer's message handler. */
    xpc_listener_incoming_session_handler_t incoming_session_handler;
    xpc_session_t peer;             /* current peer session, owned (retained) */
    bool activated;                 /* backing connection activated */
    bool cancelled;                 /* listener cancelled */
};

struct _xpc_activity_s {
    struct _xpc_object_s hdr;
    pthread_mutex_t lock;           /* serializes state, criteria, handlers,
                                     * and scheduling gates */
    char *identifier;               /* registered identifier, or NULL */
    xpc_object_t criteria;          /* retained criteria dictionary (never the
                                     * CHECK_IN sentinel) */
    xpc_activity_handler_t handler; /* copied registration block, or NULL */
    xpc_activity_state_t state;     /* CHECK_IN/WAIT/RUN/DEFER/CONTINUE/DONE */
    bool completed;                 /* DONE on a non-repeating activity */
    bool data_budgeted;             /* xpc_activity_should_be_data_budgeted */

    /* Hermetic scheduling gates (poke-driven: no background timers).  A
     * fire is enqueued when, at poke time, both gates have passed. */
    uint64_t delay_until_ns;        /* CLOCK_MONOTONIC instant an initial
                                     * XPC_ACTIVITY_DELAY elapses, or 0 */
    uint64_t next_due_ns;           /* repeating: instant the next run is due
                                     * (set after a DONE), or 0 */

    /* Eligibility-changed handlers: the original block (for pointer-matched
     * removal) alongside its retained copy. */
    xpc_activity_eligibility_changed_handler_t *eligibility;
    void **eligibility_orig;
    size_t eligibility_count, eligibility_cap;
};

struct _xpc_rich_error_s {
    struct _xpc_object_s hdr;
    char *desc;         /* human-readable failure message */
    bool can_retry;     /* xpc_rich_error_can_retry() */
};

struct _xpc_peer_requirement_s {
    struct _xpc_object_s hdr;
    uint32_t kind;      /* XPC_PEER_REQ_* */
    char *text;         /* requirement text (code-signing, team id, ...);
                         * NULL for the platform-identity flavor */
};

typedef struct _xpc_fd_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* fileport send right */
} xpc_fd_t;

#pragma mark - Type singletons (xpc_types.c)

extern const struct _xpc_type_s _xpc_type_null;
extern const struct _xpc_type_s _xpc_type_bool;
extern const struct _xpc_type_s _xpc_type_int64;
extern const struct _xpc_type_s _xpc_type_uint64;
extern const struct _xpc_type_s _xpc_type_double;
extern const struct _xpc_type_s _xpc_type_date;
extern const struct _xpc_type_s _xpc_type_data;
extern const struct _xpc_type_s _xpc_type_string;
extern const struct _xpc_type_s _xpc_type_uuid;
extern const struct _xpc_type_s _xpc_type_mach_send;
extern const struct _xpc_type_s _xpc_type_shmem;
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

extern xpc_object_t _xpc_bool_true;
extern xpc_object_t _xpc_bool_false;

xpc_kind_t xpc_kind_from_type(xpc_type_t t);

#pragma mark - Construction helpers (xpc_object.c)

xpc_object_t xpc_object_alloc(xpc_type_t t, size_t size);
xpc_object_t xpc_object_alloc_scalar(xpc_type_t t);

/* Mach-send construction.  The public xpc_mach_send_create() borrows the
 * right; xpc_mach_send_create_owned() takes a received right (COPY_SEND
 * from an OOL_PORTS descriptor) and deallocates it on release. */
xpc_object_t xpc_mach_send_create_owned(mach_port_t port);

/* Mach-recv construction (wire kind 0x15000).  A value boxes a receive
 * right so it can travel inside a dictionary; the descriptor disposition
 * on the wire is MOVE_RECEIVE, so a serialized-and-sent mach-recv value is
 * single-use — the source right is consumed, exactly like Apple's
 * __xpc_mach_recv_serialize.  xpc_mach_recv_create() takes ownership of a
 * receive right (xpc_dictionary_set_mach_recv's flavor);
 * _owned() wraps a right that arrived from the wire.  Both destroy the
 * right on release if it was never consumed. */
xpc_object_t xpc_mach_recv_create_owned(mach_port_t port);
mach_port_t xpc_mach_recv_extract_right(xpc_object_t obj);

/* Reply-context: stamp a dictionary deserialized from a received message
 * with the message's reply capability.  mode becomes 1 ("received request")
 * and *port is remembered as reply_port, along with the local-bit
 * disposition the message arrived with (needed to move the right back at
 * reply time). */
void xpc_dictionary_attach_reply_context(xpc_object_t dict,
    mach_port_t reply_port, uint8_t reply_disposition);

/* Received msgh_bits local-field disposition → the disposition that moves
 * the right back out (send-once ⇒ MOVE_SEND_ONCE, send ⇒ MOVE_SEND). */
uint8_t xpc_reply_move_disposition(uint8_t local_bit);

/* File-descriptor construction (wire kind 0xb000).  A value boxes a file
 * descriptor into a fileport send right (fileport_makeport) so it can ride
 * in the message's descriptor table; the public xpc_fd_create() boxes and
 * xpc_fd_create_from_port() wraps a right received from the wire.  Unlike
 * mach-send there is no borrowing form, so fd objects always own their
 * right and deallocate it on release. */
xpc_object_t xpc_fd_create_from_port(mach_port_t port);

/* Same-task bridge (see xpc_pipe.c): a registered handler answers
 * serialized routine requests in place of a mach_msg reply hop. */
typedef uint8_t *(*xpc_local_routine_handler_t)(const uint8_t *msg,
    size_t msg_len, uint32_t msgh_id, size_t *reply_len);
void xpc_pipe_set_local_handler(xpc_local_routine_handler_t handler);

/* Shared-memory values (wire kind 0xc000).  xpc_shmem_create maps a
 * region as a Mach memory entry (launchd v7 maps it and writes the
 * version string); xpc_shmem_create_owned wraps a memory-entry right
 * received from the wire and deallocates it on release. */
xpc_object_t xpc_shmem_create_owned(mach_port_t port, uint64_t size);
/* Release path for XPC_KIND_SHMEM: deallocate a dispose-owned entry
 * right and forget its same-task origin record. */
void xpc_shmem_dispose(xpc_shmem_t *s);
mach_port_t xpc_shmem_get_port(xpc_object_t obj);

#pragma mark - Connection subsystem (xpc_connection.c, xpc_session.c)

/* Error singletons.  Public header xpc/connection.h declares the same
 * globals as const struct _xpc_dictionary_s; definitions live in
 * object/xpc_errors.c and carry refs == 0 as an immortal sentinel so
 * xpc_retain()/xpc_release() never touch them. */
extern const struct _xpc_dictionary_s _xpc_error_connection_interrupted;
extern const struct _xpc_dictionary_s _xpc_error_connection_invalid;
extern const struct _xpc_dictionary_s _xpc_error_termination_imminent;
extern const struct _xpc_dictionary_s _xpc_error_peer_code_signing_requirement;

/* Connection state machine. */
enum {
    XPC_CONN_STATE_INACTIVE = 0,
    XPC_CONN_STATE_ACTIVE,
    XPC_CONN_STATE_CANCELLED,
};

xpc_connection_t xpc_connection_create_with_port(mach_port_t port,
    xpc_handler_t handler, void *context, xpc_finalizer_t finalizer);
void xpc_connection_register_mach_service(const char *name, mach_port_t port);
void xpc_connection_dispose(xpc_connection_t conn);
void xpc_listener_dispose(xpc_listener_t listener);

#pragma mark - Activity subsystem (xpc_activity.c)

void xpc_activity_dispose(xpc_activity_t activity);

/* Control-channel SPI from Apple (absent from the public headers): the
 * daemon-query trio.  The hermetic registry has no daemon, so run/list/debug
 * keep their signatures but act on or ignore the registered set locally. */
void xpc_activity_run(const char *identifier, xpc_activity_t activity);
void xpc_activity_debug(const char *identifier, uint64_t flags,
    dispatch_queue_t queue);
void xpc_activity_list(const char *identifier, dispatch_queue_t queue);

/* Rich error construction (object/xpc_rich_error.c). */
xpc_rich_error_t xpc_rich_error_create(const char *desc, bool can_retry);

/* Peer-requirement construction (object/xpc_peer_requirement.c).  The
 * *_text flavors take a preallocated copy; ownership transfers. */
typedef enum xpc_peer_req_kind {
    XPC_PEER_REQ_ENTITLEMENT_EXISTS = 0,
    XPC_PEER_REQ_ENTITLEMENT_VALUE,
    XPC_PEER_REQ_TEAM_IDENTITY,
    XPC_PEER_REQ_PLATFORM_IDENTITY,
    XPC_PEER_REQ_LWCR,
    XPC_PEER_REQ_CODE_SIGNING,
} xpc_peer_req_kind_t;

struct _xpc_peer_requirement_s *xpc_peer_requirement_alloc(
    xpc_peer_req_kind_t kind, char *text);

#pragma mark - Serialization (xpc_serialize.c)

/* Type tags as they appear on the wire.  (docs/GUIDE.md §4)
 *
 * Tag anatomy (confirmed by probe11 against real bytes): the type id
 * occupies bits 8..19 and the LOW BYTE carries the index into the
 * message's port-descriptor table for port-backed kinds (0xc000, 0xd000,
 * 0x11000, 0x12000, 0x15000).  Decoders must mask with 0xfff00, never
 * 0xff00 — the higher kinds (0x10000+) would otherwise alias the scale
 * kinds (0x12000 & 0xff00 == 0x2000 BOOL). */
enum {
    XPC_WIRE_NULL   = 0x1000,
    XPC_WIRE_BOOL   = 0x2000,
    XPC_WIRE_INT64  = 0x3000,
    XPC_WIRE_UINT64 = 0x4000,
    XPC_WIRE_DOUBLE = 0x5000,
    XPC_WIRE_DATE   = 0x7000,
    XPC_WIRE_DATA   = 0x8000,
    XPC_WIRE_STRING = 0x9000,
    XPC_WIRE_UUID   = 0xa000,
    XPC_WIRE_FD     = 0xb000,   /* fileport mach port */
    XPC_WIRE_SHMEM  = 0xc000,   /* memory-entry port; tag + u64 size (confirmed) */
    XPC_WIRE_MACH_SEND = 0xd000, /* send right; tag only, slot in low byte (confirmed) */
    XPC_WIRE_ARRAY  = 0xe000,
    XPC_WIRE_DICT   = 0xf000,
    XPC_WIRE_ERROR      = 0x10000,
    XPC_WIRE_CONNECTION = 0x11000, /* port-backed; layout unconfirmed (no capture) */
    XPC_WIRE_ENDPOINT   = 0x12000, /* port-backed; zero payload, slot in low byte (confirmed) */
    XPC_WIRE_SERIALIZER = 0x13000, /* internal */
    XPC_WIRE_PIPE       = 0x14000,
    XPC_WIRE_MACH_RECV  = 0x15000, /* receive right; layout unconfirmed (no capture) */
    XPC_WIRE_BUNDLE     = 0x16000,
    XPC_WIRE_SERVICE    = 0x17000,
    XPC_WIRE_SERVICE_INSTANCE = 0x18000,
    XPC_WIRE_ACTIVITY   = 0x19000,
    XPC_WIRE_FILE_TRANSFER = 0x1a000,
};

/*
 * Serialize a dictionary (or array) into the wire format.
 * Returns a heap buffer; caller frees with free().  *out_len receives
 * the total message size (mach header + envelope + body).
 *
 * msg_id selects the header msgh_id:
 *   0x10000000 simpleroutine, 0x40000000 routine, 0x20000000 reply.
 */
uint8_t *xpc_wire_serialize(xpc_object_t object, uint32_t msg_id,
    size_t *out_len);

/* Envelope constants (docs/GUIDE.md §2). */
#define XPC_WIRE_MAGIC "CPX@"
#define XPC_WIRE_VERSION 5u
#define XPC_WIRE_FLAGS_DICT 0xf000u

#pragma mark - Deserialization (xpc_deserialize.c)

/*
 * Parse a wire message back into an xpc_object_t.  Expects full mach
 * header + envelope.  Returns NULL (and sets *err cause) on malformed
 * input.  Caller owns the result.
 */
xpc_object_t xpc_wire_deserialize(const void *bytes, size_t len);

/*
 * Variant that can resolve mach-send values (wire tag 0x6000): ports is
 * the array of send rights carried in the message's OOL_PORTS descriptor.
 * Mach-send objects created from the array borrow the rights; the caller
 * keeps ownership.  ports may be NULL when the message carried none.
 */
xpc_object_t xpc_wire_deserialize_with_ports(const void *bytes, size_t len,
    const mach_port_t *ports, mach_msg_size_t nports);

#pragma mark - Description (xpc_description.c)

char *xpc_description_create(xpc_object_t object);

#pragma mark - Pipe layer (xpc_pipe.c)

typedef struct _xpc_pipe_s *xpc_pipe_t;

/* Public-ish pipe API (matches internal libxpc surface). */
xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags);
int xpc_pipe_simpleroutine(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply);
int xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply, uint32_t routine);
int xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply, uint64_t flags, uint32_t routine);
int xpc_pipe_invalidate(xpc_pipe_t pipe);

/*
 * Reply dispatch: serialize a reply-mode dictionary (made by
 * xpc_dictionary_create_reply() or a pipe receive) and send it to its
 * reply port.  Returns 0 on success, EPIPE when the reply capability is
 * gone, else a mach error code.  Shared by xpc_dictionary_send_reply() and
 * xpc_pipe_routine_reply().
 */
int xpc_reply_send(xpc_object_t reply);

/* Thin peer of xpc_reply_send() used by launchd (runtime.c) to flush a
 * reply-mode dictionary created from a received request. */
int xpc_pipe_routine_reply(xpc_object_t reply);

/* Non-XPC (launchd MIG, notifications, ...) request demultiplexer handed
 * to xpc_pipe_try_receive().  Returns true when REPLY was filled and should
 * be sent back. */
typedef boolean_t (*xpc_mig_demux_fn)(mach_msg_header_t *request,
    mach_msg_header_t *reply);

/*
 * Blocking receive loop for a Mach port set (launchd-style).  Receives one
 * message from *port_set_inout:
 *
 *   XPC requests (msgh_id in the 0x10000000/0x40000000 family) are
 *   deserialized and *request_out is set, with the request's reply
 *   capability stamped as the dict's reply context; *recv_port_out is set
 *   to the port the message arrived on for the caller's demux keying.
 *   Returns 0.
 *
 *   Anything else is handed to demux(request, reply) when provided; the
 *   reply is sent back when demux accepts it.  Returns 0 when handled,
 *   EINVAL otherwise.
 *
 * msg_size bounds the receive buffer (also the MIG reply buffer); flags
 * currently unused (Apple's is likewise a reserved stone for callers that
 * pass 0).
 */
int xpc_pipe_try_receive(mach_port_t *port_set_inout, xpc_object_t *request_out,
    mach_port_t *recv_port_out, xpc_mig_demux_fn demux,
    mach_msg_size_t msg_size, uint64_t flags);

/*
 * Same-task bridge for the launchd stub (launchd_stub.c).  A Mach reply
 * port's send-once right is invisible to the receiver when client and
 * server share one task (receive clobbers msgh_local_port with the
 * received-on port name), so routine requests to a local destination are
 * dispatched through this hook instead of mach_msg.  The wire round-trip
 * (serialize -> descriptor walk -> deserialize -> handle -> serialize
 * reply -> reply walk -> deserialize) is fully preserved; only the port
 * hop is skipped.  The handler receives the complete serialized request
 * message and returns a complete serialized reply message.
 */
typedef uint8_t *(*xpc_local_routine_handler_t)(const uint8_t *msg,
    size_t msg_len, uint32_t msgh_id, size_t *reply_len);
void xpc_pipe_set_local_handler(xpc_local_routine_handler_t handler);

/* Stash the sender's audit token onto a received dictionary. */
void xpc_dictionary_set_audit_token(xpc_object_t dict,
    const audit_token_t *token);

/*
 * msgh_id values (docs/GUIDE.md §6, §11).  Confirmed against Apple's
 * __xpc_pipe_pack_message (libxpc.dylib): base ids are
 * 0x10000000 (simpleroutine) / 0x40000000 (routine); a reply id of
 * 0x20000000 is AND'd in when a reply port is present.  The routine path
 * ORs the routine number into the low 16 bits (real launchctl "list"
 * observes as 0x400000cf); Apple's classic xpc_pipe_routine sends the
 * bare base id, so the low bits are cosmetic for the server, which demuxes
 * on the dict's "subsystem"/"routine" keys.
 */
enum {
    XPC_PIPE_ID_SIMPLEROUTINE = 0x10000000,
    XPC_PIPE_ID_ROUTINE       = 0x40000000,
    XPC_PIPE_ID_REPLY         = 0x20000000,
};

#ifdef __cplusplus
}
#endif

#endif /* __XPC_INTERNAL_H__ */
