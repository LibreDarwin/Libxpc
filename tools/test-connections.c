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
        xpc_peer_requirement_create_entitlement_exists("com.xnuports.test", &rerr);
    check(req != NULL && rerr == NULL, "requirement: entitlement_exists builds");
    if (rerr) xpc_release((xpc_object_t)rerr);
    if (req) {
        int rc = xpc_connection_set_peer_entitlement_exists_requirement(
            client, "com.xnuports.test");
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