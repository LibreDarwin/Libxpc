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
 * tools/test-connections.c — in-process duplex round-trip test for the
 * connection and session types.
 *
 * One task plays both roles: a "server" listener (xpc_connection_create_
 * mach_service with XPC_CONNECTION_MACH_SERVICE_LISTENER) registered under a
 * service name, and a "client" connection to that name.  The client sends
 * with-reply messages; the server's event handler mints a reply and sends it
 * back.  Exercises:
 *
 *   - listener registration + client peer resolution (registry)
 *   - sync and async with-reply round-trips
 *   - fire-and-forget send_message
 *   - identity getters (pid/euid/egid/asid) on the server side after a
 *     received request carries an audit trailer
 *   - send_barrier
 *   - peer-requirement store + the structural match path
 *   - xpc_session create/activate/round-trip/cancel against the same server
 *   - xpc_listener create/activate/cancel/reject_peer/create_endpoint with
 *     minted per-client peer sessions (named, anonymous, and inactive
 *     listeners)
 *   - cancel semantics (subsequent sends return connection_invalid)
 *
 * Exit status 0 = all checks pass; 1 = at least one failure.
 */

#include "xpc.h"

#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SERVICE_NAME "xpc.test.echo"
#define LISTENER_SERVICE "xpc.test.listener.echo"
#define REJECT_SERVICE "xpc.test.listener.reject"
#define INACTIVE_SERVICE "xpc.test.listener.inactive"

static int failures = 0;
static int passes = 0;

static void
check(int cond, const char *name)
{
    if (cond) {
        passes++;
    } else {
        failures++;
        printf("FAIL: %s\n", name);
    }
}

static void
start_timer(struct timespec *head)
{
    clock_gettime(CLOCK_MONOTONIC, head);
}

static bool
timed_out(const struct timespec *head, long timeout_ms)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long elapsed_ms = (now.tv_sec - head->tv_sec) * 1000LL +
        (now.tv_nsec - head->tv_nsec) / 1000000LL;
    return elapsed_ms > timeout_ms;
}

/* Poll <flag> until it is nonzero or timeout_ms elapses. */
static bool
wait_flag(volatile int *flag, long timeout_ms)
{
    struct timespec head;
    start_timer(&head);
    while (!*flag && !timed_out(&head, timeout_ms)) usleep(2000);
    return *flag != 0;
}

/* Poll <value> until it reaches <target> or timeout_ms elapses. */
static bool
wait_until(volatile int *value, int target, long timeout_ms)
{
    struct timespec head;
    start_timer(&head);
    while (*value < target && !timed_out(&head, timeout_ms)) usleep(2000);
    return *value >= target;
}

/* --- server state (written by the server's rx thread) ---------------- */

static volatile int server_saw_simpleroutine = 0;  /* send_message arrived */
static pid_t server_peer_pid = 0;
static uid_t server_peer_euid = (uid_t)-1;
static bool server_peer_audit = false;

/* SPI slice: xpc_array_apply_f accumulator. */
static struct spi_sum_ctx {
    size_t count;
    int64_t total;
} spi_sum_ctx;

static bool
spi_sum_applier(size_t idx, xpc_object_t value, void *cx)
{
    (void)idx;
    struct spi_sum_ctx *c = (struct spi_sum_ctx *)(void *)cx;
    c->count++;
    c->total += xpc_int64_get_value(value);
    return true;
}

static void
server_event_handler(xpc_object_t event)
{
    if (xpc_get_type(event) != &_xpc_type_dictionary) {
        /* Errors (connection_invalid, peer requirement failure) are not
         * expected on this path. */
        return;
    }
    if (xpc_dictionary_expects_reply(event)) {
        xpc_object_t reply = xpc_dictionary_create_reply(event);
        if (!reply) return;
        const char *pin = xpc_dictionary_get_string(event, "ping");
        xpc_dictionary_set_string(reply, "pong", pin ? pin : "?");
        xpc_dictionary_send_reply(reply);
    } else {
        server_saw_simpleroutine = 1;
    }
}

int
main(void)
{
    /* --- server -------------------------------------------------------- */

    xpc_connection_t server = xpc_connection_create_mach_service(
        SERVICE_NAME, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    check(server != NULL, "server: create_mach_service(LISTENER)");
    if (!server) return 1;
    xpc_connection_set_event_handler(server, ^(xpc_object_t e) {
        /* Capture the peer identity the rx thread stamped from the audit
         * trailer, then dispatch the caller's reply logic. */
        server_peer_pid = xpc_connection_get_pid(server);
        server_peer_euid = xpc_connection_get_euid(server);
        server_peer_audit = server_peer_pid != 0;
        server_event_handler(e);
    });
    xpc_connection_activate(server);

    /* --- client -------------------------------------------------------- */

    xpc_connection_t client = xpc_connection_create_mach_service(
        SERVICE_NAME, NULL, 0);
    check(client != NULL, "client: create_mach_service");
    if (!client) return 1;
    check(xpc_connection_get_name(client) != NULL &&
        strcmp(xpc_connection_get_name(client), SERVICE_NAME) == 0,
        "client: get_name");
    xpc_connection_activate(client);

    /* --- sync round-trip ----------------------------------------------- */

    xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "ping", "hello");
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(client, msg);
    check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
        "sync: got a dictionary reply");
    if (reply && xpc_get_type(reply) == &_xpc_type_dictionary) {
        const char *pong = xpc_dictionary_get_string(reply, "pong");
        check(pong && strcmp(pong, "hello") == 0,
            "sync: echoed value round-tripped");
        xpc_release(reply);
    }
    xpc_release(msg);

    /* --- async round-trip ---------------------------------------------- */

    msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "ping", "async");
    __block volatile int async_done = 0;
    xpc_connection_send_message_with_reply(client, msg, NULL, ^(xpc_object_t e) {
        if (e && xpc_get_type(e) == &_xpc_type_dictionary) {
            const char *pong = xpc_dictionary_get_string(e, "pong");
            if (pong && strcmp(pong, "async") == 0) async_done = 1;
        }
    });
    check(wait_flag(&async_done, 5000), "async: handler saw the reply");
    xpc_release(msg);

    /* --- fire-and-forget ----------------------------------------------- */

    msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "ping", "simple");
    xpc_connection_send_message(client, msg);
    check(wait_flag(&server_saw_simpleroutine, 5000),
        "send_message: server handler saw the simpleroutine");
    xpc_release(msg);

    /* --- identity getters (server side, after audit-trailer receipt) --- */

    check(server_peer_audit, "identity: peer audit present");
    check(server_peer_pid == (pid_t)getpid(), "identity: get_pid == getpid");
    check(server_peer_euid == (uid_t)geteuid(), "identity: get_euid == geteuid");

    /* --- barrier -------------------------------------------------------- */

    __block volatile int barrier_ran = 0;
    xpc_connection_send_barrier(client, ^{ barrier_ran = 1; });
    check(barrier_ran, "barrier: block executed");

    /* --- peer requirement ---------------------------------------------- */

    xpc_rich_error_t rerr = NULL;
    xpc_peer_requirement_t req =
        xpc_peer_requirement_create_entitlement_exists("com.LibreDarwin.test", &rerr);
    check(req != NULL && rerr == NULL, "requirement: entitlement_exists builds");
    if (rerr) xpc_release((xpc_object_t)rerr);
    if (req) {
        int rc = xpc_connection_set_peer_entitlement_exists_requirement(
            client, "com.LibreDarwin.test");
        check(rc == 0, "requirement: setter accepted entitlement_exists");
        /* The client never has the requirement enforced (it receives no
         * requests), so store the requirement on the server instead and make
         * sure a subsequent request still matches the structural check. */
        xpc_connection_set_peer_requirement(server, req);
        /* The server now owns the reference. */
        msg = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_string(msg, "ping", "req");
        reply = xpc_connection_send_message_with_reply_sync(client, msg);
        check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
            "requirement: round-trip still succeeds under structural match");
        if (reply) xpc_release(reply);
        xpc_release(msg);
    }
    check(xpc_connection_set_peer_code_signing_requirement(client, NULL) == -1,
        "requirement: NULL code-signing requirement rejected");

    /* --- session -------------------------------------------------------- */

    xpc_rich_error_t s_err = NULL;
    xpc_session_t session = xpc_session_create_mach_service(
        SERVICE_NAME, NULL, XPC_SESSION_CREATE_NONE, &s_err);
    check(session != NULL && s_err == NULL, "session: create_mach_service");
    if (s_err) xpc_release((xpc_object_t)s_err);
    if (session) {
        __block volatile int session_cancelled = 0;
        xpc_session_set_cancel_handler(session, ^(xpc_rich_error_t e) {
            session_cancelled = 1;
            (void)e;
        });
        check(xpc_session_activate(session, &s_err),
            "session: activate");
        if (s_err) xpc_release((xpc_object_t)s_err);

        char *desc = xpc_session_copy_description(session);
        check(desc != NULL, "session: copy_description");
        if (desc) {
            check(strstr(desc, SERVICE_NAME) != NULL,
                "session: description names the service");
            free(desc);
        }

        msg = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_string(msg, "ping", "session");
        xpc_rich_error_t r_err = NULL;
        reply = xpc_session_send_message_with_reply_sync(session, msg, &r_err);
        if (r_err) xpc_release((xpc_object_t)r_err);
        check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
            "session: sync round-trip");
        if (reply && xpc_get_type(reply) == &_xpc_type_dictionary) {
            const char *pong = xpc_dictionary_get_string(reply, "pong");
            check(pong && strcmp(pong, "session") == 0,
                "session: echoed value round-tripped");
            xpc_release(reply);
        }
        xpc_release(msg);

        xpc_session_cancel(session);
        check(wait_flag(&session_cancelled, 5000),
            "session: cancel handler ran");
        xpc_release((xpc_object_t)session);
    } else {
        failures++;
        printf("FAIL: session: create_mach_service returned NULL\n");
    }

    /* --- listener ------------------------------------------------------- */

    /* Named listener: mints one demuxed peer session per client.  The peer
     * handles later messages itself, and is torn down with a rich error when
     * the listener is cancelled. */
    __block volatile int listener_sessions = 0;
    __block volatile int listener_cancel_seen = 0;
    xpc_rich_error_t l_err = NULL;
    xpc_listener_t lsnr = xpc_listener_create(LISTENER_SERVICE, NULL,
        XPC_LISTENER_CREATE_NONE, ^(xpc_session_t peer) {
            listener_sessions++;
            xpc_session_set_cancel_handler(peer, ^(xpc_rich_error_t e) {
                listener_cancel_seen = 1;
                (void)e;
            });
            xpc_session_set_incoming_message_handler(peer,
                ^(xpc_object_t message) {
                    if (xpc_dictionary_expects_reply(message)) {
                        xpc_object_t rep = xpc_dictionary_create_reply(message);
                        if (rep) {
                            const char *pin =
                                xpc_dictionary_get_string(message, "ping");
                            xpc_dictionary_set_string(rep, "pong",
                                pin ? pin : "?");
                            xpc_dictionary_send_reply(rep);
                        }
                    }
                });
        }, &l_err);
    check(lsnr != NULL && l_err == NULL, "listener: create");
    if (l_err) free(xpc_rich_error_copy_description((xpc_object_t)l_err));
    if (lsnr) {
        char *ldesc = xpc_listener_copy_description(lsnr);
        check(ldesc != NULL && strstr(ldesc, LISTENER_SERVICE) != NULL,
            "listener: copy_description names the service");
        if (ldesc) free(ldesc);
        check(xpc_listener_set_peer_code_signing_requirement(lsnr, NULL) == -1,
            "listener: NULL code-signing requirement rejected");

        xpc_connection_t lclient = xpc_connection_create_mach_service(
            LISTENER_SERVICE, NULL, 0);
        check(lclient != NULL, "listener: named client create");
        if (lclient) {
            xpc_connection_activate(lclient);
            msg = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(msg, "ping", "listener");
            reply = xpc_connection_send_message_with_reply_sync(lclient, msg);
            check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
                "listener: client sync round-trip");
            if (reply && xpc_get_type(reply) == &_xpc_type_dictionary) {
                const char *pong = xpc_dictionary_get_string(reply, "pong");
                check(pong && strcmp(pong, "listener") == 0,
                    "listener: echoed value round-tripped");
                xpc_release(reply);
            }
            xpc_release(msg);
            check(listener_sessions == 1, "listener: first message minted peer");

            msg = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(msg, "ping", "again");
            reply = xpc_connection_send_message_with_reply_sync(lclient, msg);
            check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
                "listener: second round-trip also succeeds");
            if (reply) xpc_release(reply);
            xpc_release(msg);
            check(listener_sessions == 1,
                "listener: second message reused the minted peer");

            /* The endpoint wraps the listener's port (intact before cancel). */
            xpc_endpoint_t lep = xpc_listener_create_endpoint(lsnr);
            check(lep != NULL && xpc_get_type((xpc_object_t)lep) ==
                    &_xpc_type_endpoint &&
                xpc_endpoint_get_port((xpc_object_t)lep) != MACH_PORT_NULL,
                "listener: create_endpoint yields a live port");
            if (lep) xpc_release((xpc_object_t)lep);

            xpc_listener_cancel(lsnr);
            check(wait_flag(&listener_cancel_seen, 5000),
                "listener: peer cancel handler ran after listener_cancel");
            check(xpc_listener_create_endpoint(lsnr) == NULL,
                "listener: create_endpoint after cancel");
            xpc_release((xpc_object_t)lclient);
        }
        xpc_release((xpc_object_t)lsnr);
    } else {
        failures++;
        printf("FAIL: listener: create returned NULL\n");
    }

    /* --- reject_peer ----------------------------------------------------- */

    /* The listener mints a fresh peer for the next client message after
     * rejecting the first one.  The rejected peer's cancel handler sees the
     * rejection reason synchronously (inside the incoming-session handler). */
    __block volatile int reject_peers = 0;
    __block volatile int reject_cancel_seen = 0;
    xpc_listener_t rej = NULL;
    xpc_rich_error_t rej_err = NULL;
    rej = xpc_listener_create(REJECT_SERVICE, NULL, XPC_LISTENER_CREATE_NONE,
        ^(xpc_session_t peer) {
            reject_peers++;
            if (reject_peers == 1) {
                xpc_session_set_cancel_handler(peer, ^(xpc_rich_error_t e) {
                    reject_cancel_seen = 1;
                    if (e) {
                        char *why = xpc_rich_error_copy_description(
                            (xpc_object_t)e);
                        if (why) {
                            if (strstr(why, "test rejection") == NULL) {
                                failures++;
                                printf("FAIL: reject: reason surfaced\n");
                            } else {
                                passes++;
                            }
                            free(why);
                        }
                    }
                });
                xpc_listener_reject_peer(peer, "test rejection");
            } else {
                xpc_session_set_incoming_message_handler(peer,
                    ^(xpc_object_t message) {
                        if (xpc_dictionary_expects_reply(message)) {
                            xpc_object_t rep =
                                xpc_dictionary_create_reply(message);
                            if (rep) {
                                xpc_dictionary_set_string(rep, "pong",
                                    "accepted");
                                xpc_dictionary_send_reply(rep);
                            }
                        }
                    });
            }
        }, &rej_err);
    check(rej != NULL && rej_err == NULL, "reject: create");
    if (rej_err) free(xpc_rich_error_copy_description((xpc_object_t)rej_err));
    if (rej) {
        xpc_connection_t rclient = xpc_connection_create_mach_service(
            REJECT_SERVICE, NULL, 0);
        check(rclient != NULL, "reject: client create");
        if (rclient) {
            xpc_connection_activate(rclient);
            /* First message is fire-and-forget: the peer is rejected, so a
             * with-reply send would hang waiting for a reply nobody sends. */
            msg = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(msg, "ping", "drop");
            xpc_connection_send_message(rclient, msg);
            check(wait_flag(&reject_cancel_seen, 5000),
                "reject: rejected peer's cancel handler ran");
            xpc_release(msg);

            msg = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(msg, "ping", "retry");
            reply = xpc_connection_send_message_with_reply_sync(rclient, msg);
            check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
                "reject: subsequent message minted an accepted peer");
            if (reply && xpc_get_type(reply) == &_xpc_type_dictionary) {
                check(strcmp(xpc_dictionary_get_string(reply, "pong"),
                    "accepted") == 0,
                    "reject: accepted peer echoed");
                xpc_release(reply);
            }
            xpc_release(msg);
            check(reject_peers == 2, "reject: two peers minted total");
            xpc_release((xpc_object_t)rclient);
        }
        xpc_listener_cancel(rej);
        xpc_release((xpc_object_t)rej);
    } else {
        failures++;
        printf("FAIL: reject: create returned NULL\n");
    }

    /* --- inactive listener ---------------------------------------------- */

    __block volatile int inactive_sessions = 0;
    xpc_rich_error_t in_err = NULL;
    xpc_listener_t inact = xpc_listener_create(INACTIVE_SERVICE, NULL,
        XPC_LISTENER_CREATE_INACTIVE, ^(xpc_session_t peer) {
            inactive_sessions++;
            xpc_session_set_incoming_message_handler(peer,
                ^(xpc_object_t message) {
                    if (xpc_dictionary_expects_reply(message)) {
                        xpc_object_t rep = xpc_dictionary_create_reply(message);
                        if (rep) {
                            xpc_dictionary_set_string(rep, "pong", "inactive");
                            xpc_dictionary_send_reply(rep);
                        }
                    }
                });
        }, &in_err);
    check(inact != NULL && in_err == NULL, "inactive: create with INACTIVE");
    if (in_err) free(xpc_rich_error_copy_description((xpc_object_t)in_err));
    if (inact) {
        check(xpc_listener_activate(inact, NULL), "inactive: activate");
        check(xpc_listener_activate(inact, NULL),
            "inactive: double activate idempotent");
        xpc_connection_t iclient = xpc_connection_create_mach_service(
            INACTIVE_SERVICE, NULL, 0);
        check(iclient != NULL, "inactive: client create");
        if (iclient) {
            xpc_connection_activate(iclient);
            msg = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(msg, "ping", "inactive");
            reply = xpc_connection_send_message_with_reply_sync(iclient, msg);
            check(reply != NULL && xpc_get_type(reply) == &_xpc_type_dictionary,
                "inactive: round-trip after activate");
            if (reply) xpc_release(reply);
            xpc_release(msg);
            check(inactive_sessions == 1, "inactive: peer minted after activate");
            xpc_release((xpc_object_t)iclient);
        }
        xpc_release((xpc_object_t)inact);
    } else {
        failures++;
        printf("FAIL: inactive: create returned NULL\n");
    }

    /* Second inactive listener released without activation: exercises the
     * dispose path for a backing connection whose receive thread never
     * started. */
    xpc_rich_error_t in2_err = NULL;
    xpc_listener_t inact2 = xpc_listener_create(INACTIVE_SERVICE, NULL,
        XPC_LISTENER_CREATE_INACTIVE, ^(xpc_session_t peer) {
            (void)peer;
        }, &in2_err);
    check(inact2 != NULL, "inactive: create second listener");
    if (in2_err) free(xpc_rich_error_copy_description((xpc_object_t)in2_err));
    if (inact2) {
        xpc_release((xpc_object_t)inact2);
        check(1, "inactive: dispose without activation");
    }

    /* --- anonymous listener --------------------------------------------- */

    __block volatile int anon_sessions = 0;
    xpc_listener_t anon = xpc_listener_create_anonymous();
    check(anon != NULL, "anonymous: create_anonymous");
    if (anon) {
        xpc_listener_set_incoming_session_handler(anon, ^(xpc_session_t peer) {
            anon_sessions++;
            xpc_session_set_incoming_message_handler(peer,
                ^(xpc_object_t message) {
                    if (xpc_dictionary_expects_reply(message)) {
                        xpc_object_t rep = xpc_dictionary_create_reply(message);
                        if (rep) {
                            xpc_dictionary_set_string(rep, "pong", "anon");
                            xpc_dictionary_send_reply(rep);
                        }
                    }
                });
        });
        xpc_endpoint_t aep = xpc_listener_create_endpoint(anon);
        check(aep != NULL && xpc_get_type((xpc_object_t)aep) ==
                &_xpc_type_endpoint &&
            xpc_endpoint_get_port((xpc_object_t)aep) != MACH_PORT_NULL,
            "anonymous: create_endpoint yields a live port");
        if (aep) {
            xpc_connection_t aclient = xpc_connection_create_from_endpoint(aep);
            check(aclient != NULL, "anonymous: client from endpoint");
            if (aclient) {
                xpc_connection_activate(aclient);
                msg = xpc_dictionary_create(NULL, NULL, 0);
                xpc_dictionary_set_string(msg, "ping", "anon");
                reply = xpc_connection_send_message_with_reply_sync(aclient,
                    msg);
                check(reply != NULL &&
                    xpc_get_type(reply) == &_xpc_type_dictionary,
                    "anonymous: round-trip over endpoint client");
                if (reply && xpc_get_type(reply) == &_xpc_type_dictionary) {
                    check(strcmp(xpc_dictionary_get_string(reply, "pong"),
                        "anon") == 0,
                        "anonymous: echoed value round-tripped");
                    xpc_release(reply);
                }
                xpc_release(msg);
                check(anon_sessions == 1, "anonymous: peer minted");
                xpc_release((xpc_object_t)aclient);
            }
            xpc_release((xpc_object_t)aep);
        }
        xpc_listener_cancel(anon);
        xpc_release((xpc_object_t)anon);
    } else {
        failures++;
        printf("FAIL: anonymous: create_anonymous returned NULL\n");
    }

    /* --- activity ------------------------------------------------------- */

    /* Test A: non-repeating fire.  The handler runs with state RUN, DONE is
     * accepted, and the (non-repeating) activity is terminal: copy_criteria
     * answers NULL and further transitions are rejected. */
    __block xpc_activity_t act = NULL;
    __block volatile int act_fired = 0;
    __block volatile int act_fires = 0;
    __block volatile long act_state_in_handler = -1;
    __block volatile int act_done_accepted = -1;
    const char *act_id = "com.sunneva.hermetic.activity.test";

    xpc_object_t act_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(act_id, act_criteria, ^(xpc_activity_t a) {
        if (!act) act = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)a);
        act_state_in_handler = xpc_activity_get_state(a);
        act_done_accepted = xpc_activity_set_state(a,
            XPC_ACTIVITY_STATE_DONE);
        act_fired = 1;
        act_fires++;
    });
    xpc_release(act_criteria);

    check(wait_flag(&act_fired, 5000), "activity: handler fired");
    check(act != NULL, "activity: handler received a live activity");
    check(act_state_in_handler == XPC_ACTIVITY_STATE_RUN,
        "activity: state is RUN during the handler");
    check(act_done_accepted == 1,
        "activity: set_state(DONE) accepted from RUN");
    check(xpc_activity_get_state(act) == XPC_ACTIVITY_STATE_DONE,
        "activity: state is DONE after transition");
    check(xpc_activity_copy_criteria(act) == NULL,
        "activity: copy_criteria NULL after non-repeating DONE");

    /* Terminal-state rejections from the main thread. */
    check(!xpc_activity_set_state(act, XPC_ACTIVITY_STATE_DONE),
        "activity: set_state(DONE) rejected from DONE");
    check(!xpc_activity_set_state(act, XPC_ACTIVITY_STATE_DEFER),
        "activity: set_state(DEFER) rejected from DONE");

    /* Test B: repeating activity.  DONE returns it to WAIT (not terminal),
     * criteria survive, and there is no spontaneous re-fire while the
     * interval has not elapsed. */
    __block xpc_activity_t rep = NULL;
    __block volatile int rep_fired = 0;
    const char *rep_id = "com.sunneva.hermetic.activity.repeat";

    xpc_object_t rep_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_bool(rep_criteria, XPC_ACTIVITY_REPEATING, true);
    xpc_dictionary_set_uint64(rep_criteria, XPC_ACTIVITY_INTERVAL, 60);
    xpc_activity_register(rep_id, rep_criteria, ^(xpc_activity_t a) {
        if (!rep) rep = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)a);
        xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        rep_fired++;
    });
    xpc_release(rep_criteria);

    check(wait_flag(&rep_fired, 5000), "activity: repeating handler fired");
    check(rep != NULL, "activity: repeating handle captured");
    check(xpc_activity_get_state(rep) == XPC_ACTIVITY_STATE_WAIT,
        "activity: state WAIT after repeating DONE");
    check(xpc_activity_copy_criteria(rep) != NULL,
        "activity: copy_criteria survives repeating DONE");
    check(!xpc_activity_set_state(rep, XPC_ACTIVITY_STATE_DONE),
        "activity: set_state(DONE) rejected from WAIT");
    usleep(200000);
    check(rep_fired == 1, "activity: no spontaneous re-fire (interval gate)");

    /* Test C: DEFER from RUN is accepted and re-schedules exactly one more
     * run, in which DONE completes the activity. */
    __block volatile int def_fires = 0;
    __block volatile int def_accept = -1;
    const char *def_id = "com.sunneva.hermetic.activity.defer";

    xpc_object_t def_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(def_id, def_criteria, ^(xpc_activity_t a) {
        if (def_fires == 0) {
            def_accept = xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DEFER);
        } else {
            xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        }
        def_fires++;
    });
    xpc_release(def_criteria);

    check(wait_until(&def_fires, 2, 5000), "activity: DEFER re-scheduled");
    check(def_accept == 1, "activity: set_state(DEFER) accepted from RUN");
    check(def_fires == 2, "activity: DEFER produced exactly one re-fire");

    /* Test D: RUN → CONTINUE accepted; CONTINUE is sticky (state CONTINUE,
     * further CONTINUE rejected); a completion status is only legal with
     * DONE; CONTINUE → DONE with status completes. */
    __block volatile int cont_res = 0;
    __block volatile int cont_sticky = -1;
    __block volatile int cont_bad_status = -1;
    __block volatile int cont_status_done = -1;
    __block volatile long cont_state_after_continue = -1;
    const char *cont_id = "com.sunneva.hermetic.activity.continue";

    xpc_object_t cont_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(cont_id, cont_criteria, ^(xpc_activity_t a) {
        cont_res = xpc_activity_set_state(a, XPC_ACTIVITY_STATE_CONTINUE);
        cont_state_after_continue = xpc_activity_get_state(a);
        cont_sticky = xpc_activity_set_state(a, XPC_ACTIVITY_STATE_CONTINUE);
        cont_bad_status = xpc_activity_set_state_with_completion_status(a,
            XPC_ACTIVITY_STATE_DEFER, 7);
        cont_status_done = xpc_activity_set_state_with_completion_status(a,
            XPC_ACTIVITY_STATE_DONE, 7);
        cont_res++;
    });
    xpc_release(cont_criteria);

    check(wait_flag(&cont_res, 5000), "activity: CONTINUE handler ran");
    check(cont_state_after_continue == XPC_ACTIVITY_STATE_CONTINUE,
        "activity: state is CONTINUE after CONTINUE");
    check(cont_sticky == 0,
        "activity: set_state(CONTINUE) rejected from CONTINUE");
    check(cont_bad_status == 0,
        "activity: completion status rejected for non-DONE target");
    check(cont_status_done == 1,
        "activity: CONTINUE → DONE with completion status accepted");

    /* Test E: CONTINUE → DEFER re-schedules, mirroring Test C. */
    __block volatile int cd_fires = 0;
    __block volatile int cd_defer_accept = -1;
    const char *cd_id = "com.sunneva.hermetic.activity.continue-defer";

    xpc_object_t cd_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(cd_id, cd_criteria, ^(xpc_activity_t a) {
        if (cd_fires == 0) {
            xpc_activity_set_state(a, XPC_ACTIVITY_STATE_CONTINUE);
            cd_defer_accept = xpc_activity_set_state(a,
                XPC_ACTIVITY_STATE_DEFER);
        } else {
            xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        }
        cd_fires++;
    });
    xpc_release(cd_criteria);

    check(wait_until(&cd_fires, 2, 5000),
        "activity: CONTINUE → DEFER re-scheduled");
    check(cd_defer_accept == 1,
        "activity: set_state(DEFER) accepted from CONTINUE");

    /* Test F: data-budget probes are deterministic no-ops. */
    check(!xpc_activity_should_defer(act), "activity: should_defer is false");
    check(xpc_activity_get_percentage(act) == 0,
        "activity: get_percentage is 0");
    check(!xpc_activity_defer_until_percentage(act, 50),
        "activity: defer_until_percentage is false");
    check(!xpc_activity_defer_until_network_change(act),
        "activity: defer_until_network_change is false");
    xpc_activity_set_network_threshold(act, 10);
    xpc_activity_should_be_data_budgeted(act, true);
    xpc_activity_should_be_data_budgeted(act, false);
    check(1, "activity: data-budget setters accepted");
    check(xpc_activity_copy_dispatch_queue(act) == NULL,
        "activity: copy_dispatch_queue NULL (no libdispatch)");

    /* Test G: set_criteria re-installs a completed activity and drives a
     * fresh run. */
    xpc_object_t gd = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_bool(gd, XPC_ACTIVITY_REPEATING, false);
    xpc_activity_set_criteria(act, gd);
    xpc_release(gd);
    check(wait_until(&act_fires, 2, 5000),
        "activity: set_criteria re-installed a completed activity");
    check(act_state_in_handler == XPC_ACTIVITY_STATE_RUN,
        "activity: re-installed criteria drove a fresh RUN");
    check(act_done_accepted == 1,
        "activity: original handler still answers re-fires");

    /* Test H: CHECK_IN presents the current state without starting a run. */
    __block volatile int checkin_seen = 0;
    __block volatile long checkin_state = -1;
    xpc_activity_register(act_id, XPC_ACTIVITY_CHECK_IN, ^(xpc_activity_t a) {
        checkin_state = xpc_activity_get_state(a);
        checkin_seen = 1;
    });
    check(wait_flag(&checkin_seen, 5000),
        "activity: CHECK_IN presented to an existing registration");
    check(checkin_state == XPC_ACTIVITY_STATE_DONE,
        "activity: CHECK_IN presents current (DONE) state");

    /* Test H2: a first-ever CHECK_IN mints an activity in CHECK_IN state; a
     * subsequent set_criteria starts its first real run. */
    __block volatile int ci_frames = 0;
    __block volatile long ci_state0 = -1;
    __block volatile long ci_state1 = -1;
    __block xpc_activity_t ci_act = NULL;
    const char *ci_id = "com.sunneva.hermetic.activity.checkin-first";

    xpc_activity_register(ci_id, XPC_ACTIVITY_CHECK_IN, ^(xpc_activity_t a) {
        if (!ci_act) ci_act = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)a);
        long st = xpc_activity_get_state(a);
        if (ci_frames == 0) {
            ci_state0 = st;
        } else {
            ci_state1 = st;
            xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        }
        ci_frames++;
    });
    check(wait_until(&ci_frames, 1, 5000),
        "activity: first CHECK_IN handler ran");
    check(ci_state0 == XPC_ACTIVITY_STATE_CHECK_IN,
        "activity: first CHECK_IN presents CHECK_IN state");
    check(ci_frames == 1, "activity: CHECK_IN does not auto-start a run");

    xpc_object_t ci_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_set_criteria(ci_act, ci_criteria);
    xpc_release(ci_criteria);
    check(wait_until(&ci_frames, 2, 5000),
        "activity: set_criteria started the first real run");
    check(ci_state1 == XPC_ACTIVITY_STATE_RUN,
        "activity: post-check-in run presented RUN");

    /* Test I: eligibility handlers are notified on accepted transitions and
     * can be removed by pointer. */
    __block xpc_activity_t elig_act = NULL;
    __block volatile int elig_frames = 0;
    const char *elig_id = "com.sunneva.hermetic.activity.eligibility";

    xpc_object_t elig_criteria = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(elig_id, elig_criteria, ^(xpc_activity_t a) {
        if (!elig_act) elig_act = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)a);
        xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        elig_frames++;
    });
    xpc_release(elig_criteria);
    check(wait_flag(&elig_frames, 5000),
        "activity: eligibility driver handler ran");

    __block volatile int elig_notify = 0;
    xpc_activity_eligibility_changed_handler_t eh =
        ^(xpc_activity_t a) { (void)a; elig_notify++; };
    xpc_activity_add_eligibility_changed_handler(elig_act, eh);

    xpc_object_t e2 = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_set_criteria(elig_act, e2);
    xpc_release(e2);
    check(wait_until(&elig_frames, 2, 5000),
        "activity: eligibility driver re-fired");
    usleep(100000);
    check(elig_notify == 1, "activity: eligibility handler notified");

    xpc_activity_remove_eligibility_changed_handler(elig_act, eh);
    xpc_object_t e3 = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_set_criteria(elig_act, e3);
    xpc_release(e3);
    check(wait_until(&elig_frames, 3, 5000),
        "activity: eligibility driver re-fired again");
    usleep(100000);
    check(elig_notify == 1, "activity: removed eligibility handler silent");

    /* Test J: unregister drops the registration; re-register mints a fresh
     * activity (a different object) that fires again. */
    xpc_activity_unregister(act_id);
    __block volatile int re_fired = 0;
    __block xpc_activity_t re_act = NULL;
    xpc_object_t red = xpc_dictionary_create(NULL, NULL, 0);
    xpc_activity_register(act_id, red, ^(xpc_activity_t a) {
        if (!re_act) re_act = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)a);
        xpc_activity_set_state(a, XPC_ACTIVITY_STATE_DONE);
        re_fired = 1;
    });
    xpc_release(red);
    check(wait_flag(&re_fired, 5000),
        "activity: re-register after unregister fires");
    check(re_act != NULL && re_act != act,
        "activity: unregister replaced the registration object");

    /* identity round-trip and cleanup */
    char *act_name = xpc_activity_copy_identifier(act);
    check(act_name != NULL && strcmp(act_name, act_id) == 0,
        "activity: copy_identifier round-trips");
    free(act_name);

    xpc_activity_unregister(rep_id);
    xpc_activity_unregister(def_id);
    xpc_activity_unregister(cont_id);
    xpc_activity_unregister(cd_id);
    xpc_activity_unregister(ci_id);
    xpc_activity_unregister(elig_id);
    xpc_activity_unregister(act_id);
    xpc_release((xpc_object_t)act);
    xpc_release((xpc_object_t)rep);
    xpc_release((xpc_object_t)ci_act);
    xpc_release((xpc_object_t)elig_act);
    xpc_release((xpc_object_t)re_act);

    /* --- transactions -------------------------------------------------- */

    /* begin()/end() must link and balance without faulting (the underflow
     * path aborts, so surviving these calls is the assertion). */
    xpc_transaction_begin();
    xpc_transaction_begin();
    xpc_transaction_end();
    xpc_transaction_end();
    check(1, "transaction: begin/end balance");

    /* --- dispatch-data-backed values ----------------------------------- */

    uint8_t dblob[33];
    for (int k = 0; k < 33; k++) dblob[k] = (uint8_t)(k * 7);
    dispatch_data_t dd = dispatch_data_create(dblob, sizeof(dblob), NULL, NULL);
    xpc_object_t ddata_obj = xpc_data_create_with_dispatch_data(dd);
    size_t ddata_len = xpc_data_get_length(ddata_obj);
    const void *ddata_ptr = xpc_data_get_bytes_ptr(ddata_obj);
    check(ddata_len == sizeof(dblob) && ddata_ptr != NULL &&
        memcmp(ddata_ptr, dblob, sizeof(dblob)) == 0,
        "data: create_with_dispatch_data round-trips");
    xpc_release(ddata_obj);
    dispatch_release(dd);

    /* --- cancel semantics ---------------------------------------------- */

    char *reason = xpc_connection_copy_invalidation_reason(client);
    check(reason != NULL, "cancel: invalidation reason readable before cancel");
    free(reason);

    xpc_connection_cancel(client);
    msg = xpc_dictionary_create(NULL, NULL, 0);
    reply = xpc_connection_send_message_with_reply_sync(client, msg);
    check(reply == (xpc_object_t)&_xpc_error_connection_invalid,
        "cancel: post-cancel sync send returns connection_invalid");
    xpc_release(reply);
    xpc_release(msg);

    reason = xpc_connection_copy_invalidation_reason(client);
    check(reason != NULL, "cancel: invalidation reason after cancel");
    free(reason);

    xpc_connection_cancel(server);
    xpc_release((xpc_object_t)server);
    xpc_release((xpc_object_t)client);

    /* --- xpc_set_event_stream_handler ----------------------------------- */

    {
        char stream[64];
        snprintf(stream, sizeof(stream), "xpc.test.stream.%ld",
            (long)getpid());
        static void *events_key;
        dispatch_queue_t eq = dispatch_queue_create("xpc.test.events", NULL);
        __block volatile int event_seen = 0;
        __block bool event_on_target_queue = false;
        dispatch_queue_set_specific(eq, &events_key, (void *)0x1, NULL);
        xpc_set_event_stream_handler(stream, eq, ^(xpc_object_t event) {
            if (event && xpc_get_type(event) == &_xpc_type_dictionary) {
                const char *feed = xpc_dictionary_get_string(event, "feed");
                if (feed && strcmp(feed, "seeds") == 0) event_seen = 1;
            }
            event_on_target_queue =
                dispatch_get_specific(&events_key) == (void *)0x1;
        });

        xpc_connection_t pub =
            xpc_connection_create_mach_service(stream, NULL, 0);
        check(pub != NULL, "events: create stream publisher connection");
        if (pub) {
            xpc_connection_activate(pub);
            xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_string(m, "feed", "seeds");
            xpc_connection_send_message(pub, m);
            xpc_release(m);
            struct timespec head;
            start_timer(&head);
            while (!event_seen && !timed_out(&head, 5000))
                usleep(20000);
            check(event_seen, "events: handler received published event");
            check(event_on_target_queue,
                "events: delivered on the target queue");
            xpc_connection_cancel(pub);
            xpc_release((xpc_object_t)pub);
        }
        dispatch_release(eq);
    }

    /* --- SPI slice: value setters, apply_f, pointer, event_channel ------ */

    {
        xpc_object_t b = xpc_bool_create(true);
        xpc_bool_set_value(b, false);
        check(!xpc_bool_get_value(b), "spi: xpc_bool_set_value");

        xpc_object_t i = xpc_int64_create(7);
        xpc_int64_set_value(i, -42);
        check(xpc_int64_get_value(i) == -42, "spi: xpc_int64_set_value");

        xpc_object_t d = xpc_double_create(1.5);
        xpc_double_set_value(d, 3.25);
        check(xpc_double_get_value(d) == 3.25, "spi: xpc_double_set_value");

        xpc_object_t s = xpc_string_create("abc");
        xpc_string_set_value(s, "def");
        check(xpc_string_get_length(s) == 3 &&
            strcmp(xpc_string_get_string_ptr(s), "def") == 0,
            "spi: xpc_string_set_value");

        xpc_object_t data = xpc_data_create("aaa", 3);
        xpc_data_set_value(data, "xyzzy", 5);
        check(xpc_data_get_length(data) == 5 &&
            memcmp(xpc_data_get_bytes_ptr(data), "xyzzy", 5) == 0,
            "spi: xpc_data_set_value");

        xpc_release(b);
        xpc_release(i);
        xpc_release(d);
        xpc_release(s);
        xpc_release(data);
    }

    {
        static int spi_payload;
        xpc_object_t p = xpc_pointer_create(&spi_payload);
        check(p != NULL && xpc_get_type(p) == &_xpc_type_pointer,
            "spi: xpc_pointer_create type");
        check(xpc_pointer_get_value(p) == (void *)&spi_payload,
            "spi: xpc_pointer_get_value round-trip");
        char *pdesc = xpc_copy_description(p);
        check(pdesc && strstr(pdesc, "pointer") != NULL,
            "spi: pointer description names the type");
        free(pdesc);
        xpc_release(p);
    }

    {
        xpc_object_t a = xpc_array_create(NULL, 0);
        check(a != NULL, "spi: apply_f array create");
        const int64_t vals[] = { 1, 2, 3 };
        for (size_t k = 0; k < 3; k++) {
            xpc_object_t v = xpc_int64_create(vals[k]);
            xpc_array_append_value(a, v);
            xpc_release(v);
        }
        xpc_array_apply_f(a, &spi_sum_ctx, spi_sum_applier);
        check(spi_sum_ctx.count == 3 && spi_sum_ctx.total == 6,
            "spi: xpc_array_apply_f sum");
        xpc_release(a);
    }

    {
        /* Listener connections may be marked as event channels; peer
         * connections refuse (the abort path is not exercised here). */
        xpc_connection_t ec = xpc_connection_create_mach_service(
            "xpc.test.eventchannel", NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
        check(ec != NULL, "spi: event_channel listener create");
        if (ec) {
            xpc_connection_set_event_channel(ec, true);
            check(1, "spi: xpc_connection_set_event_channel accepted by listener");
            xpc_connection_cancel(ec);
            xpc_release((xpc_object_t)ec);
        }
    }

    {
        /* xpc_binprefs SPI: allocation, add/overflow, the Apple equality
         * quirks, the "%d: [t.s, ...]" description, and the posix_spawnattr
         * bridge.  The abort() paths (out-of-range index, failing
         * setarchpref) are not exercised. */
        xpc_binprefs_t bp = xpc_binprefs_alloc();
        check(bp != NULL, "spi: binprefs alloc");
        check(xpc_binprefs_count(bp) == 0, "spi: binprefs fresh count");

        xpc_binprefs_add(bp, 7, 3);          /* x86_64 / x86_64_ALL */
        xpc_binprefs_add(bp, 12, 0);         /* arm64 / arm64       */
        xpc_binprefs_add(bp, 0x7fffffff, 1024);
        check(xpc_binprefs_count(bp) == 3, "spi: binprefs add count");
        check(xpc_binprefs_cpu_type(bp, 0) == 7,
            "spi: binprefs cpu_type[0]");
        check(xpc_binprefs_cpu_subtype(bp, 0) == 3,
            "spi: binprefs cpu_subtype[0]");
        check(xpc_binprefs_cpu_type(bp, 1) == 12,
            "spi: binprefs cpu_type[1]");
        check(xpc_binprefs_cpu_type(bp, 2) == 0x7fffffff,
            "spi: binprefs cpu_type[2]");

        xpc_binprefs_add(bp, 7, 3);          /* fills slot 4 -> count 4 */
        xpc_binprefs_add(bp, 100, 100);      /* dropped: block is full   */
        check(xpc_binprefs_count(bp) == 4, "spi: binprefs overflow keeps count");
        check(xpc_binprefs_cpu_type(bp, 3) == 7, "spi: binprefs slot 4 kept");

        xpc_binprefs_t empty = xpc_binprefs_alloc();
        xpc_binprefs_t any = xpc_binprefs_alloc();
        xpc_binprefs_add(any, -1, 0);        /* CPU_TYPE_ANY */
        check(xpc_binprefs_is_noop(empty), "spi: binprefs empty is noop");
        /* Apple quirk: is_noop() is true unless the first entry is
         * CPU_TYPE_ANY (-1), so a concrete first entry is also "noop". */
        check(xpc_binprefs_is_noop(bp), "spi: binprefs concrete first entry is noop");
        check(!xpc_binprefs_is_noop(any), "spi: binprefs CPU_TYPE_ANY not noop");

        xpc_binprefs_t c = xpc_binprefs_copy(bp);
        check(c != NULL && c != bp, "spi: binprefs copy is a new block");
        check(xpc_binprefs_equal(bp, c), "spi: binprefs copy equals source");
        check(!xpc_binprefs_equal(bp, any), "spi: binprefs distinct unequal");
        check(xpc_binprefs_equal(empty, empty), "spi: binprefs empty equals empty");
        check(!xpc_binprefs_equal(NULL, NULL), "spi: binprefs NULL,NULL unequal");
        check(xpc_binprefs_equal(NULL, bp), "spi: binprefs one-sided NULL equal");
        check(xpc_binprefs_equal(bp, NULL), "spi: binprefs other one-sided NULL equal");

        char *d = xpc_binprefs_copy_description(NULL);
        check(d && strcmp(d, "(null)") == 0, "spi: binprefs NULL description");
        free(d);
        d = xpc_binprefs_copy_description(empty);
        check(d && strcmp(d, "0: []") == 0, "spi: binprefs empty description");
        free(d);
        d = xpc_binprefs_copy_description(bp);
        check(d && strcmp(d,
            "4: [7.3, 12.0, 2147483647.1024, 7.3]") == 0,
            "spi: binprefs description format");
        free(d);

        posix_spawnattr_t sa;
        if (posix_spawnattr_init(&sa) == 0) {
            xpc_binprefs_t psp = xpc_binprefs_alloc();
            xpc_binprefs_add(psp, 7, 3);
            xpc_binprefs_add(psp, 12, 0);
            xpc_binprefs_set_psattr(psp, &sa);
            check(1, "spi: binprefs set_psattr accepted");
            posix_spawnattr_destroy(&sa);
            free(psp);
        } else {
            check(0, "spi: binprefs set_psattr (posix_spawnattr_init)");
        }

        free(bp);
        free(c);
        free(empty);
        free(any);
    }

    /* --- xpc_spawnattr serialization SPI ------------------------------ */
    {
        /* Blob model: 400 bytes total, 293 of header, region = [293, 400). */
        char sblob[400];
        uint8_t bblob[400], pblob[400];
        const char *arr[3], *r;
        xpc_binprefs_t b, e, o;
        uint32_t n, pk, got;
        uint64_t space;
        size_t i;

        memset(sblob, 0xAA, sizeof sblob);
        memset(bblob, 0xAA, sizeof bblob);
        memset(pblob, 0xAA, sizeof pblob);

        n = 0;
        space = 4096;
        _xpc_spawnattr_pack_string(sblob, &n, &space, "arch");
        check(n == 5 && space == 4091, "spi: spawnattr pack_string cursor/space");
        check(memcmp(sblob + 293, "arch", 4) == 0 && sblob[297] == '\0',
            "spi: spawnattr pack_string bytes at 293+cursor");
        _xpc_spawnattr_pack_string_fragment(sblob, &n, &space, "frage");
        check(n == 10 && space == 4086, "spi: spawnattr pack_fragment advances strlen only");
        check(memcmp(sblob + 298, "frage", 5) == 0,
            "spi: spawnattr pack_fragment concatenates");

        r = _xpc_spawnattr_unpack_string(sblob, 107, 0);
        check(r == sblob + 293 && strcmp(r, "arch") == 0,
            "spi: spawnattr unpack_string first");
        r = _xpc_spawnattr_unpack_string(sblob, 107, 5);
        check(r == sblob + 298 && strcmp(r, "frage") == 0,
            "spi: spawnattr unpack_string second");
        r = _xpc_spawnattr_unpack_string(sblob, 107, 0);
        check(r && strnlen(r, 10) == 4,
            "spi: spawnattr unpack_string finds real NUL");
        check(_xpc_spawnattr_unpack_string(sblob, 107, 107) == NULL,
            "spi: spawnattr unpack_string offset == limit");
        check(_xpc_spawnattr_unpack_string(sblob, 107, 100) == NULL,
            "spi: spawnattr unpack_string truncated (no NUL in bound)");

        n = 0;
        space = 4096;
        _xpc_spawnattr_pack_bytes(bblob, &n, &space, "ABCD", 4);
        check(n == 4 && space == 4092, "spi: spawnattr pack_bytes cursor/space");
        check(memcmp(bblob + 293, "ABCD", 4) == 0,
            "spi: spawnattr pack_bytes bytes at 293+cursor");
        check(_xpc_spawnattr_unpack_bytes(bblob, 107, 0, 4) == (char *)bblob + 293,
            "spi: spawnattr unpack_bytes fits");
        check(_xpc_spawnattr_unpack_bytes(bblob, 107, 0, 8) == (char *)bblob + 293,
            "spi: spawnattr unpack_bytes large len fits");
        check(_xpc_spawnattr_unpack_bytes(bblob, 107, 103, 4) == (char *)bblob + 396,
            "spi: spawnattr unpack_bytes exactly fits at end");
        check(_xpc_spawnattr_unpack_bytes(bblob, 107, 105, 3) == NULL,
            "spi: spawnattr unpack_bytes runs past limit");

        n = 0;
        space = 4096;
        _xpc_spawnattr_pack_string(sblob, &n, &space, "aa");
        _xpc_spawnattr_pack_string(sblob, &n, &space, "bb");
        check(n == 6, "spi: spawnattr two packed strings cursor");
        arr[0] = "sentinel";
        r = _xpc_spawnattr_unpack_strings(sblob, 107, 0, arr, 2);
        check(r == arr[0] && r == sblob + 293 && strcmp(arr[0], "aa") == 0,
            "spi: spawnattr unpack_strings first");
        check(arr[1] == sblob + 296 && strcmp(arr[1], "bb") == 0,
            "spi: spawnattr unpack_strings second");
        check(_xpc_spawnattr_unpack_strings(sblob, 107, 107, arr, 2) == NULL,
            "spi: spawnattr unpack_strings offset == limit");
        check(_xpc_spawnattr_unpack_strings(sblob, 107, 105, arr, 1) == NULL,
            "spi: spawnattr unpack_strings truncated");
        arr[0] = "sentinel";
        check(_xpc_spawnattr_unpack_strings(sblob, 107, 0, arr, 0) == arr[0],
            "spi: spawnattr unpack_strings count 0 returns strings[0]");

        b = xpc_binprefs_alloc();
        xpc_binprefs_add(b, 7, 3);
        xpc_binprefs_add(b, 12, 0);
        xpc_binprefs_add(b, 17, 2);
        check(_xpc_spawnattr_binprefs_size(b) == 24,
            "spi: spawnattr binprefs_size = 8*count");

        pk = 0;
        space = 4096;
        _xpc_spawnattr_binprefs_pack(pblob, b, &pk, &space);
        check(pk == 24 && space == 4072, "spi: spawnattr binprefs_pack pk/space");
        memcpy(&got, pblob + 68, 4);
        check(got == 3, "spi: spawnattr binprefs_pack count at +68");
        memcpy(&got, pblob + 72, 4);
        check(got == 0, "spi: spawnattr binprefs_pack start offset at +72");
        o = _xpc_spawnattr_binprefs_unpack(pblob, 107);
        check(o && xpc_binprefs_equal(b, o), "spi: spawnattr binprefs round trip");
        for (i = 0; i < 3; i++) {
            check(xpc_binprefs_cpu_type(o, i) == xpc_binprefs_cpu_type(b, i) &&
                xpc_binprefs_cpu_subtype(o, i) == xpc_binprefs_cpu_subtype(b, i),
                "spi: spawnattr binprefs pair values");
        }
        free(o);

        pk = 5;
        space = 4096;
        _xpc_spawnattr_binprefs_pack(bblob, b, &pk, &space);
        check(pk == 5 + 24, "spi: spawnattr binprefs_pack after strings");
        memcpy(&got, bblob + 72, 4);
        check(got == 5, "spi: spawnattr binprefs_pack start offset carries pk");
        o = _xpc_spawnattr_binprefs_unpack(bblob, 107);
        check(o && xpc_binprefs_equal(b, o), "spi: spawnattr binprefs unpacks at +pk");

        memcpy(bblob + 72, &(uint32_t){100}, 4);
        check(_xpc_spawnattr_binprefs_unpack(bblob, 107) == NULL,
            "spi: spawnattr binprefs_unpack past limit");
        memcpy(bblob + 72, &(uint32_t){0}, 4);

        e = xpc_binprefs_alloc();
        check(_xpc_spawnattr_binprefs_size(e) == 0,
            "spi: spawnattr binprefs_size empty = 0");
        pk = 0;
        space = 4096;
        _xpc_spawnattr_binprefs_pack(bblob, e, &pk, &space);
        check(pk == 0 && space == 4096, "spi: spawnattr empty binprefs_pack noop");
        memcpy(&got, bblob + 68, 4);
        check(got == 0, "spi: spawnattr empty binprefs count at +68");
        check(_xpc_spawnattr_binprefs_unpack(bblob, 107) == NULL,
            "spi: spawnattr empty binprefs_unpack NULL");

        free(e);
        free(b);
    }

    /* --- xpc_dictionary SPI --------------------------------------------- */
    {
        extern void xpc_dictionary_attach_reply_context(xpc_object_t dict,
            mach_port_t reply_port, uint8_t reply_disposition);

        xpc_object_t sd, rp, got, content, req, reply;
        mach_port_t prt, prt2;
        int sink = 42;
        char *desc;

        if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &prt) != KERN_SUCCESS) prt = MACH_PORT_NULL;
        if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
            &prt2) != KERN_SUCCESS) prt2 = MACH_PORT_NULL;

        /* set_pointer / get_pointer round trip + type gating */
        sd = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_pointer(sd, "p", &sink);
        got = (xpc_object_t)xpc_dictionary_get_pointer(sd, "p");
        check(got != NULL && xpc_pointer_get_value(got) == &sink,
            "spi: dict pointer set/get round trip");
        check(xpc_dictionary_get_pointer(sd, "absent") == NULL,
            "spi: dict get_pointer missing key -> NULL");
        xpc_dictionary_set_string(sd, "s", "not-a-pointer");
        check(xpc_dictionary_get_pointer(sd, "s") == NULL,
            "spi: dict get_pointer non-pointer value -> NULL");

        /* set_value_with_key_string_cache (cache arg ignored by port) */
        content = xpc_string_create("cached-v");
        xpc_dictionary_set_value_with_key_string_cache(sd, "ck", content,
            xpc_string_create("cache-token"));
        check(xpc_dictionary_get_value(sd, "ck") == content,
            "spi: dict set_value_with_key_string_cache stores");
        xpc_dictionary_set_value_with_key_string_cache(sd, "ck0", content,
            NULL);
        check(xpc_dictionary_get_value(sd, "ck0") == content,
            "spi: dict set_value_with_key_string_cache NULL cache");
        xpc_release(content);

        /* extract_mach_send / extract_mach_recv single-use semantics */
        xpc_dictionary_set_mach_send(sd, "ms", prt);
        check(_xpc_dictionary_extract_mach_send(sd, "ms") == prt,
            "spi: dict extract_mach_send moves right");
        check(_xpc_dictionary_extract_mach_send(sd, "ms") == MACH_PORT_NULL,
            "spi: dict extract_mach_send twice -> NULL");
        xpc_dictionary_set_mach_recv(sd, "mr", prt2);
        check(xpc_dictionary_extract_mach_recv(sd, "mr") == prt2,
            "spi: dict extract_mach_recv moves right");
        check(xpc_dictionary_extract_mach_recv(sd, "mr") == MACH_PORT_NULL,
            "spi: dict extract_mach_recv twice -> NULL");
        check(xpc_dictionary_extract_mach_recv(sd, "absent") == MACH_PORT_NULL,
            "spi: dict extract_mach_recv missing -> NULL");

        /* reply-with-port mint + reply-port/extract + transaction = NULL */
        rp = _xpc_dictionary_create_reply_with_port(prt);
        check(rp != NULL, "spi: dict create_reply_with_port");
        check(_xpc_dictionary_extract_reply_port(rp) == prt,
            "spi: dict extract_reply_port round trip");
        check(_xpc_dictionary_get_transaction(sd) == NULL &&
            _xpc_dictionary_get_transaction(rp) == NULL,
            "spi: dict get_transaction always NULL");
        check(_xpc_dictionary_extract_reply_port(sd) == MACH_PORT_NULL,
            "spi: dict extract_reply_port on plain dict -> NULL");

        /* reply-msg-id trio (mode 2 reply; plain dict is inert) */
        check(_xpc_dictionary_get_reply_msg_id(rp) == 0,
            "spi: dict get_reply_msg_id initial 0");
        _xpc_dictionary_set_reply_msg_id(rp, 7);
        check(_xpc_dictionary_get_reply_msg_id(rp) == 7,
            "spi: dict set/get_reply_msg_id");
        check(_xpc_dictionary_extract_reply_msg_id(rp) == 7,
            "spi: dict extract_reply_msg_id returns value");
        check(_xpc_dictionary_get_reply_msg_id(rp) == 0,
            "spi: dict extract_reply_msg_id clears tag");
        _xpc_dictionary_set_reply_msg_id(sd, 9);
        check(_xpc_dictionary_get_reply_msg_id(sd) == 0,
            "spi: dict set_reply_msg_id inert on plain dict");

        /* remote connection: attaches only to a mode-1 request */
        req = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_attach_reply_context(req, prt,
            MACH_MSG_TYPE_MAKE_SEND_ONCE);
        xpc_connection_t conn = xpc_connection_create_mach_service(
            "xpc.test.dictspi", NULL, 0);
        check(conn != NULL, "spi: dict remote conn object");
        _xpc_dictionary_set_remote_connection(req, conn);
        check(xpc_dictionary_get_connection(req) == conn,
            "spi: dict set/get_remote_connection round trip");
        _xpc_dictionary_set_remote_connection(sd, conn);
        check(xpc_dictionary_get_connection(sd) == NULL,
            "spi: dict set_remote_connection inert on plain dict");
        check(xpc_dictionary_get_connection(xpc_null_create()) == NULL,
            "spi: dict get_connection non-dict -> NULL");
        if (conn) xpc_release((xpc_object_t)conn);

        /* send_reply_4SWIFT: mode-2 reply shipped as-is; capability consumed */
        reply = _xpc_dictionary_create_reply_with_port(prt2);
        check(xpc_dictionary_expects_reply(req), "spi: dict 4SWIFT req expects reply");
        check(xpc_dictionary_expects_reply(reply), "spi: dict 4SWIFT reply usable");
        xpc_dictionary_send_reply_4SWIFT(req, reply);
        check(xpc_get_type(reply) == &_xpc_type_dictionary,
            "spi: dict 4SWIFT passthrough keeps reply object alive");
        check(_xpc_dictionary_extract_reply_port(reply) == MACH_PORT_NULL,
            "spi: dict 4SWIFT passthrough consumed reply capability");

        /* send_reply_4SWIFT: mode-0 reply copied into a fresh reply; the
         * source request's reply context is consumed by create_reply. */
        content = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_string(content, "k", "v");
        xpc_dictionary_send_reply_4SWIFT(req, content);
        check(xpc_dictionary_get_string(content, "k") != NULL,
            "spi: dict 4SWIFT copy leaves source dict intact");
        check(!xpc_dictionary_expects_reply(req),
            "spi: dict 4SWIFT copy consumes request context");
        xpc_dictionary_send_reply_4SWIFT(xpc_null_create(), content);
        xpc_dictionary_send_reply_4SWIFT(req, xpc_null_create());
        xpc_release(content);
        xpc_release(reply);
        xpc_release(req);

        /* copy_basic_description: malloc'd dict dump contains a set key */
        xpc_dictionary_set_string(sd, "basic", "key");
        desc = xpc_dictionary_copy_basic_description(sd);
        check(desc != NULL && strstr(desc, "basic") != NULL,
            "spi: dict copy_basic_description contains key");
        free(desc);
        xpc_release(sd);
        xpc_release(rp);

        mach_port_mod_refs(mach_task_self(), prt, MACH_PORT_RIGHT_RECEIVE, -1);
        if (prt2 != MACH_PORT_NULL)
            mach_port_mod_refs(mach_task_self(), prt2, MACH_PORT_RIGHT_RECEIVE, -1);
    }

    /* --- xpc_main: service runloop (final, in-process) ------------------ */

    /* xpc_main() never returns, so it runs last on the main thread while a
     * helper on a concurrent queue plays the client.  Passing is impossible
     * unless xpc_main actually registers the listener: the client only
     * prints the summary and exits once a service reply arrives, or when the
     * round-trip times out.  (Named services are in-process in this port, so
     * a separate service process cannot be reached; xpc_main is exercised
     * in-place instead.) */
    char *xpc_main_svc = malloc(64);
    if (!xpc_main_svc) return 1;
    snprintf(xpc_main_svc, 64, "xpc.test.xpcmain.%ld", (long)getpid());
    setenv("XPC_SERVICE_NAME", xpc_main_svc, 1);
    __block volatile int xpc_main_pong = 0;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        xpc_connection_t c = xpc_connection_create_mach_service(
            xpc_main_svc, NULL, 0);
        check(c != NULL, "xpc_main: create_mach_service");
        if (c) {
            xpc_connection_activate(c);
            struct timespec head;
            start_timer(&head);
            while (!xpc_main_pong && !timed_out(&head, 5000)) {
                xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
                xpc_dictionary_set_string(m, "ping", "via_xpc_main");
                xpc_connection_send_message_with_reply(c, m, NULL,
                    ^(xpc_object_t e) {
                        if (e && xpc_get_type(e) == &_xpc_type_dictionary) {
                            const char *pong =
                                xpc_dictionary_get_string(e, "pong");
                            if (pong && strcmp(pong, "via_xpc_main") == 0)
                                xpc_main_pong = 1;
                        }
                    });
                xpc_release(m);
                usleep(50000);
            }
            check(xpc_main_pong,
                "xpc_main: service answered a with-reply call");
            xpc_connection_cancel(c);
            xpc_release((xpc_object_t)c);
        }
        printf("test-connections: %d passed, %d failed\n", passes, failures);
        exit(failures == 0 ? 0 : 1);
    });

    xpc_main(^(xpc_connection_t peer) {
        xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
            if (xpc_get_type(event) != &_xpc_type_dictionary) return;
            if (xpc_dictionary_expects_reply(event)) {
                xpc_object_t reply = xpc_dictionary_create_reply(event);
                if (!reply) return;
                const char *ping = xpc_dictionary_get_string(event, "ping");
                xpc_dictionary_set_string(reply, "pong", ping ? ping : "?");
                xpc_dictionary_send_reply(reply);
            }
        });
    });

    /* unreachable: xpc_main() never returns */
    return 0;
}