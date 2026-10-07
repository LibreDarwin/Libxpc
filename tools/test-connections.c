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
 *   - cancel semantics (subsequent sends return connection_invalid)
 *
 * Exit status 0 = all checks pass; 1 = at least one failure.
 */

#include "xpc.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SERVICE_NAME "xpc.test.echo"

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

    printf("test-connections: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}