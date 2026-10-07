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
 * xpc_activity.c — the XPC "idle maintenance" activity type.
 *
 * Apple's implementation is a thin client of the launchd activity daemon:
 * xpc_activity_register() submits an identifier and a criteria dictionary
 * and the daemon decides when the handler fires, delivering the decision by
 * talking back through a control channel.  This hermetic build has no
 * daemon, so the registry is process-local and the scheduler is poke-driven:
 *
 *   - register / set_criteria / run / set_state re-evaluate eligibility at
 *     the point of the poke and fire the handler on a private serial thread
 *     when eligible.
 *   - a freshly registered (or updated) activity is eligible immediately
 *     unless an XPC_ACTIVITY_DELAY has not yet elapsed.  There are no
 *     background timers, so a delayed or repeating activity sits in WAIT
 *     until the next poke.
 *   - a repeating activity that reaches DONE records next_due = now +
 *     XPC_ACTIVITY_INTERVAL and waits for the next poke.
 *   - a non-repeating activity that reaches DONE is terminal: copy_criteria
 *     returns NULL and no further fires occur until criteria are reinstalled
 *     (register / set_criteria).
 *
 * The state machine matches Apple's validation exactly (verified against the
 * arm64e disassembly): set_state_with_completion_status is accepted only
 * from RUN or CONTINUE, requires status == 0 unless the target is DONE, and
 * from CONTINUE allows only DONE or DEFER while RUN also allows CONTINUE.
 * The data-budget probes (defer_until_percentage, defer_until_network_change,
 * get_percentage, set_network_threshold, should_defer) are byte-identical
 * macOS no-ops, and copy_dispatch_queue returns NULL (no libdispatch).
 */

#include "xpc_internal.h"

#include <Block.h>
#include <pthread.h>

#define NS_PER_SEC 1000000000ull

#pragma mark - Time

static uint64_t
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

#pragma mark - CHECK_IN sentinel

/* Immortal sentinel passed to xpc_activity_register() as the criteria to
 * check in with an existing registration.  refs == 0 matches the
 * XPC_ERROR_* immortal convention in object/xpc_errors.c: retain/release
 * and xpc_release's destroy path never touch it.  The pointer is exported
 * as _XPC_ACTIVITY_CHECK_IN, mirroring Apple's exported data symbol. */
static const struct _xpc_dictionary_s _xpc_activity_checkin_dictionary =
    { .hdr = { .isa = &_xpc_type_dictionary, .refs = 0 } };

const xpc_object_t XPC_ACTIVITY_CHECK_IN =
    (const xpc_object_t)&_xpc_activity_checkin_dictionary;

static bool
criteria_is_checkin(xpc_object_t criteria)
{
    return criteria == (xpc_object_t)XPC_ACTIVITY_CHECK_IN;
}

#pragma mark - Registry

/* In-process identifier → activity table.  Entries retain their activity
 * until unregistered, so a registration stays alive independently of the
 * xpc_activity_t the handler sees. */
struct activity_registry_entry {
    char           *identifier;
    xpc_activity_t activity;    /* retained */
};

static pthread_mutex_t g_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct activity_registry_entry *g_registry;
static size_t g_registry_count, g_registry_cap;

static xpc_activity_t
registry_find_retained(const char *identifier)
{
    pthread_mutex_lock(&g_registry_lock);
    xpc_activity_t found = NULL;
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].identifier, identifier) == 0) {
            found = (xpc_activity_t)(void *)xpc_retain(
                (xpc_object_t)g_registry[i].activity);
            break;
        }
    }
    pthread_mutex_unlock(&g_registry_lock);
    return found;
}

static void
registry_upsert(const char *identifier, xpc_activity_t activity)
{
    pthread_mutex_lock(&g_registry_lock);
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].identifier, identifier) == 0) {
            xpc_activity_t old = g_registry[i].activity;
            g_registry[i].activity = (xpc_activity_t)(void *)xpc_retain(
                (xpc_object_t)activity);
            xpc_release((xpc_object_t)old);
            pthread_mutex_unlock(&g_registry_lock);
            return;
        }
    }
    if (g_registry_count == g_registry_cap) {
        size_t newcap = g_registry_cap ? g_registry_cap * 2 : 8;
        struct activity_registry_entry *np = realloc(g_registry,
            newcap * sizeof(*np));
        if (!np) {
            pthread_mutex_unlock(&g_registry_lock);
            return;
        }
        g_registry = np;
        g_registry_cap = newcap;
    }
    struct activity_registry_entry *e = &g_registry[g_registry_count++];
    e->identifier = strdup(identifier);
    e->activity = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)activity);
    pthread_mutex_unlock(&g_registry_lock);
}

static void
registry_remove(const char *identifier)
{
    pthread_mutex_lock(&g_registry_lock);
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].identifier, identifier) == 0) {
            xpc_release((xpc_object_t)g_registry[i].activity);
            free(g_registry[i].identifier);
            g_registry[i] = g_registry[--g_registry_count];
            pthread_mutex_unlock(&g_registry_lock);
            return;
        }
    }
    pthread_mutex_unlock(&g_registry_lock);
}

#pragma mark - Serial fire thread

/* Handler invocations are serialized on one process-wide thread, standing in
 * for the daemon's runloop: a fire never re-enters a handler, and a handler
 * that pokes the same activity merely enqueues the next fire.  Items retain
 * the activity for the duration of the invocation. */
struct activity_work_s {
    xpc_activity_t activity;    /* retained */
    struct activity_work_s *next;
};

static pthread_mutex_t g_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_queue_cond = PTHREAD_COND_INITIALIZER;
static struct activity_work_s *g_queue_head, *g_queue_tail;
static pthread_t g_queue_thread;
static bool g_queue_started;

static void *
activity_queue_thread(void *unused)
{
    (void)unused;
    for (;;) {
        struct activity_work_s *item;
        pthread_mutex_lock(&g_queue_lock);
        while (!g_queue_head) {
            pthread_cond_wait(&g_queue_cond, &g_queue_lock);
        }
        item = g_queue_head;
        g_queue_head = item->next;
        if (!g_queue_head) g_queue_tail = NULL;
        pthread_mutex_unlock(&g_queue_lock);

        xpc_activity_t activity = item->activity;
        free(item);

        struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
        xpc_activity_handler_t handler = NULL;
        pthread_mutex_lock(&a->lock);
        if (a->handler) handler = Block_copy(a->handler);
        pthread_mutex_unlock(&a->lock);
        if (handler) {
            handler(activity);
            Block_release(handler);
        }
        xpc_release((xpc_object_t)activity);
    }
    return NULL;
}

static void
activity_queue_enqueue(xpc_activity_t activity)
{
    struct activity_work_s *w = calloc(1, sizeof(*w));
    if (!w) return;
    w->activity = (xpc_activity_t)(void *)xpc_retain((xpc_object_t)activity);

    pthread_mutex_lock(&g_queue_lock);
    if (!g_queue_started) {
        if (pthread_create(&g_queue_thread, NULL, activity_queue_thread,
                NULL) == 0) {
            g_queue_started = true;
        } else {
            xpc_release((xpc_object_t)w->activity);
            free(w);
            pthread_mutex_unlock(&g_queue_lock);
            return;
        }
    }
    if (!g_queue_head) {
        g_queue_head = g_queue_tail = w;
    } else {
        g_queue_tail->next = w;
        g_queue_tail = w;
    }
    pthread_cond_signal(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_lock);
}

#pragma mark - Scheduling

static bool
criteria_repeating(struct _xpc_activity_s *a)
{
    return xpc_dictionary_get_bool(a->criteria, XPC_ACTIVITY_REPEATING);
}

/* Re-derive the delay gate from a (re)installed criteria dictionary.
 * next_due is cleared: a fresh criteria installation always makes the
 * activity due (the interval only starts counting after a DONE). */
static void
activity_reset_gates(struct _xpc_activity_s *a)
{
    uint64_t delay = xpc_dictionary_get_uint64(a->criteria,
        XPC_ACTIVITY_DELAY);
    a->delay_until_ns = delay ? now_ns() + delay * NS_PER_SEC : 0;
    a->next_due_ns = 0;
}

/* Holding no lock is required: reads the (row-locked) gates. */
static bool
activity_runnable(struct _xpc_activity_s *a)
{
    uint64_t n = now_ns();
    if (a->delay_until_ns && n < a->delay_until_ns) return false;
    if (a->next_due_ns && n < a->next_due_ns) return false;
    return true;
}

/* Fire a run: present state RUN, then enqueue an invocation. */
static void
activity_fire_run(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    pthread_mutex_lock(&a->lock);
    a->state = XPC_ACTIVITY_STATE_RUN;
    a->delay_until_ns = 0;
    pthread_mutex_unlock(&a->lock);
    activity_queue_enqueue(activity);
}

/* The universal poke: enqueue a fire if the activity is not busy, not
 * terminal, and past its gates. */
static void
activity_maybe_fire(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    bool fire = false;
    pthread_mutex_lock(&a->lock);
    if (!a->completed &&
            a->state != XPC_ACTIVITY_STATE_RUN &&
            a->state != XPC_ACTIVITY_STATE_CONTINUE &&
            activity_runnable(a)) {
        fire = true;
    }
    pthread_mutex_unlock(&a->lock);
    if (fire) activity_fire_run(activity);
}

/* Apply an accepted transition target.  Returns true when the activity
 * should be re-poked (DEFER → back to WAIT, immediately eligible again
 * given the hermetic "conditions always satisfied" model). */
static bool
activity_apply_target(struct _xpc_activity_s *a, xpc_activity_state_t target)
{
    switch (target) {
    case XPC_ACTIVITY_STATE_DONE: {
        if (criteria_repeating(a)) {
            uint64_t interval = xpc_dictionary_get_uint64(a->criteria,
                XPC_ACTIVITY_INTERVAL);
            a->state = XPC_ACTIVITY_STATE_WAIT;
            a->next_due_ns = now_ns() + interval * NS_PER_SEC;
            return false;
        }
        a->state = XPC_ACTIVITY_STATE_DONE;
        a->completed = true;
        return false;
    }
    case XPC_ACTIVITY_STATE_CONTINUE:
        a->state = XPC_ACTIVITY_STATE_CONTINUE;
        return false;
    case XPC_ACTIVITY_STATE_DEFER:
        a->state = XPC_ACTIVITY_STATE_WAIT;
        return true;
    default:
        return false;
    }
}

#pragma mark - Eligibility handlers

static void
activity_notify_eligibility(struct _xpc_activity_s *a)
{
    size_t count = 0;
    xpc_activity_eligibility_changed_handler_t *copies = NULL;

    pthread_mutex_lock(&a->lock);
    if (a->eligibility_count) {
        copies = malloc(a->eligibility_count * sizeof(*copies));
        if (copies) {
            for (size_t i = 0; i < a->eligibility_count; i++) {
                copies[count++] = Block_copy(a->eligibility[i]);
            }
        }
    }
    pthread_mutex_unlock(&a->lock);

    for (size_t i = 0; i < count; i++) {
        copies[i]((xpc_activity_t)a);
        Block_release(copies[i]);
    }
    free(copies);
}

#pragma mark - Construction

static struct _xpc_activity_s *
activity_alloc(const char *identifier, xpc_object_t criteria,
    xpc_activity_handler_t handler)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)
        xpc_object_alloc(&_xpc_type_activity, sizeof(*a));
    if (!a) return NULL;
    pthread_mutex_init(&a->lock, NULL);
    a->identifier = strdup(identifier);
    a->criteria = xpc_retain(criteria);
    a->handler = Block_copy(handler);
    a->state = XPC_ACTIVITY_STATE_WAIT;
    activity_reset_gates(a);
    return a;
}

void
xpc_activity_dispose(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (a->handler) Block_release(a->handler);
    for (size_t i = 0; i < a->eligibility_count; i++) {
        Block_release(a->eligibility[i]);
    }
    free(a->eligibility);
    free(a->eligibility_orig);
    if (a->criteria) xpc_release(a->criteria);
    free(a->identifier);
    pthread_mutex_destroy(&a->lock);
}

#pragma mark - Register / unregister

void
xpc_activity_register(const char *identifier, xpc_object_t criteria,
    xpc_activity_handler_t handler)
{
    if (!identifier || !handler || !criteria) return;

    if (criteria_is_checkin(criteria)) {
        /* Check in with the existing registration: replace the handler and
         * present the activity's current state (not a fresh run). */
        xpc_activity_t existing = registry_find_retained(identifier);
        if (existing) {
            struct _xpc_activity_s *a =
                (struct _xpc_activity_s *)(void *)existing;
            pthread_mutex_lock(&a->lock);
            xpc_activity_handler_t old = a->handler;
            a->handler = Block_copy(handler);
            Block_release(old);
            pthread_mutex_unlock(&a->lock);
            activity_queue_enqueue(existing);
            xpc_release((xpc_object_t)existing);
        } else {
            /* First ever check-in: register with empty, immediately-eligible
             * criteria.  The handler sees the CHECK_IN state; the app is
             * expected to install real criteria (xpc_activity_set_criteria)
             * to drive further fires. */
            struct _xpc_activity_s *a = activity_alloc(identifier,
                xpc_dictionary_create(NULL, NULL, 0), handler);
            if (!a) return;
            a->state = XPC_ACTIVITY_STATE_CHECK_IN;
            registry_upsert(identifier, (xpc_activity_t)a);
            activity_queue_enqueue((xpc_activity_t)a);
            xpc_release((xpc_object_t)a);
        }
        return;
    }

    if (!XPC_OBJECT_CHECK(criteria, &_xpc_type_dictionary)) return;

    xpc_activity_t existing = registry_find_retained(identifier);
    if (existing) {
        struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)existing;
        pthread_mutex_lock(&a->lock);
        xpc_object_t old_criteria = a->criteria;
        xpc_activity_handler_t old_handler = a->handler;
        a->criteria = xpc_retain(criteria);
        a->handler = Block_copy(handler);
        xpc_release(old_criteria);
        Block_release(old_handler);
        a->completed = false;
        a->state = XPC_ACTIVITY_STATE_WAIT;
        activity_reset_gates(a);
        pthread_mutex_unlock(&a->lock);
        activity_maybe_fire(existing);
        xpc_release((xpc_object_t)existing);
        return;
    }

    struct _xpc_activity_s *a = activity_alloc(identifier, criteria, handler);
    if (!a) return;
    registry_upsert(identifier, (xpc_activity_t)a);
    activity_maybe_fire((xpc_activity_t)a);
    xpc_release((xpc_object_t)a);
}

void
xpc_activity_unregister(const char *identifier)
{
    if (!identifier) return;
    registry_remove(identifier);
}

#pragma mark - Criteria

xpc_object_t
xpc_activity_copy_criteria(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) return NULL;
    pthread_mutex_lock(&a->lock);
    xpc_object_t copy = a->completed ? NULL : xpc_retain(a->criteria);
    pthread_mutex_unlock(&a->lock);
    return copy;
}

void
xpc_activity_set_criteria(xpc_activity_t activity, xpc_object_t criteria)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) return;
    if (!criteria || criteria_is_checkin(criteria)) return;
    if (!XPC_OBJECT_CHECK(criteria, &_xpc_type_dictionary)) return;

    pthread_mutex_lock(&a->lock);
    xpc_object_t old = a->criteria;
    a->criteria = xpc_retain(criteria);
    xpc_release(old);
    a->completed = false;
    if (a->state != XPC_ACTIVITY_STATE_RUN &&
            a->state != XPC_ACTIVITY_STATE_CONTINUE) {
        a->state = XPC_ACTIVITY_STATE_WAIT;
    }
    activity_reset_gates(a);
    pthread_mutex_unlock(&a->lock);
    activity_maybe_fire(activity);
}

#pragma mark - State

xpc_activity_state_t
xpc_activity_get_state(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) {
        return XPC_ACTIVITY_STATE_WAIT;
    }
    pthread_mutex_lock(&a->lock);
    xpc_activity_state_t state = a->state;
    pthread_mutex_unlock(&a->lock);
    return state;
}

bool
xpc_activity_set_state_with_completion_status(xpc_activity_t activity,
    xpc_activity_state_t state, long status)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) return false;

    bool refire = false;
    bool accepted = false;
    pthread_mutex_lock(&a->lock);
    xpc_activity_state_t current = a->state;
    /* Mirrors _xpc_activity_set_state_with_completion_status: only RUN and
     * CONTINUE may transition, and the target set depends on the current
     * state.  A nonzero completion status is only legal with DONE. */
    if ((state == XPC_ACTIVITY_STATE_DONE || status == 0) &&
            (current == XPC_ACTIVITY_STATE_RUN ||
             current == XPC_ACTIVITY_STATE_CONTINUE) &&
            ((current == XPC_ACTIVITY_STATE_RUN &&
              (state == XPC_ACTIVITY_STATE_DEFER ||
               state == XPC_ACTIVITY_STATE_CONTINUE ||
               state == XPC_ACTIVITY_STATE_DONE)) ||
             (current == XPC_ACTIVITY_STATE_CONTINUE &&
              (state == XPC_ACTIVITY_STATE_DEFER ||
               state == XPC_ACTIVITY_STATE_DONE)))) {
        refire = activity_apply_target(a, state);
        accepted = true;
    }
    pthread_mutex_unlock(&a->lock);

    if (!accepted) return false;
    activity_notify_eligibility(a);
    if (refire) activity_maybe_fire(activity);
    return true;
}

bool
xpc_activity_set_state(xpc_activity_t activity, xpc_activity_state_t state)
{
    return xpc_activity_set_state_with_completion_status(activity, state, 0);
}

bool
xpc_activity_set_completion_status(xpc_activity_t activity, long status)
{
    return xpc_activity_set_state_with_completion_status(activity,
        XPC_ACTIVITY_STATE_DONE, status);
}

bool
xpc_activity_should_defer(xpc_activity_t activity)
{
    /* No system resources to reclaim in a hermetic process: never defer.
     * (Apple's macOS implementation always answers false as well.) */
    (void)activity;
    return false;
}

#pragma mark - Data-budget probes (byte-identical macOS no-ops)

bool
xpc_activity_defer_until_percentage(xpc_activity_t activity, long percentage)
{
    (void)activity;
    (void)percentage;
    return false;
}

bool
xpc_activity_defer_until_network_change(xpc_activity_t activity)
{
    (void)activity;
    return false;
}

long
xpc_activity_get_percentage(xpc_activity_t activity)
{
    (void)activity;
    return 0;
}

void
xpc_activity_set_network_threshold(xpc_activity_t activity, long percentage)
{
    (void)activity;
    (void)percentage;
}

void
xpc_activity_should_be_data_budgeted(xpc_activity_t activity, bool budgeted)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) return;
    pthread_mutex_lock(&a->lock);
    a->data_budgeted = budgeted;
    pthread_mutex_unlock(&a->lock);
}

#pragma mark - Identity

char *
xpc_activity_copy_identifier(xpc_activity_t activity)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity)) return NULL;
    pthread_mutex_lock(&a->lock);
    char *copy = a->identifier ? strdup(a->identifier) : NULL;
    pthread_mutex_unlock(&a->lock);
    return copy;
}

dispatch_queue_t
xpc_activity_copy_dispatch_queue(xpc_activity_t activity)
{
    /* No libdispatch: the serial fire thread stands in for the queue. */
    (void)activity;
    return NULL;
}

#pragma mark - Eligibility handlers

void
xpc_activity_add_eligibility_changed_handler(xpc_activity_t activity,
    xpc_activity_eligibility_changed_handler_t handler)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity) || !handler) return;

    pthread_mutex_lock(&a->lock);
    if (a->eligibility_count == a->eligibility_cap) {
        size_t newcap = a->eligibility_cap ? a->eligibility_cap * 2 : 4;
        xpc_activity_eligibility_changed_handler_t *np = realloc(
            a->eligibility, newcap * sizeof(*np));
        void **op = realloc(a->eligibility_orig, newcap * sizeof(*op));
        if (!np || !op) {
            free(np);
            free(op);
            pthread_mutex_unlock(&a->lock);
            return;
        }
        a->eligibility = np;
        a->eligibility_orig = op;
        a->eligibility_cap = newcap;
    }
    a->eligibility_orig[a->eligibility_count] = (void *)handler;
    a->eligibility[a->eligibility_count] = Block_copy(handler);
    a->eligibility_count++;
    pthread_mutex_unlock(&a->lock);
}

void
xpc_activity_remove_eligibility_changed_handler(xpc_activity_t activity,
    xpc_activity_eligibility_changed_handler_t handler)
{
    struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)activity;
    if (!XPC_OBJECT_CHECK(activity, &_xpc_type_activity) || !handler) return;

    pthread_mutex_lock(&a->lock);
    for (size_t i = 0; i < a->eligibility_count; i++) {
        if (a->eligibility_orig[i] == (void *)handler) {
            Block_release(a->eligibility[i]);
            size_t rest = a->eligibility_count - i - 1;
            memmove(&a->eligibility[i], &a->eligibility[i + 1],
                rest * sizeof(a->eligibility[0]));
            memmove(&a->eligibility_orig[i], &a->eligibility_orig[i + 1],
                rest * sizeof(a->eligibility_orig[0]));
            a->eligibility_count--;
            break;
        }
    }
    pthread_mutex_unlock(&a->lock);
}

#pragma mark - Daemon-query SPI (hermetic: no daemon)

void
xpc_activity_run(const char *identifier, xpc_activity_t activity)
{
    xpc_activity_t target = activity;
    if (target) {
        struct _xpc_activity_s *a = (struct _xpc_activity_s *)(void *)target;
        pthread_mutex_lock(&a->lock);
        a->completed = false;
        a->state = XPC_ACTIVITY_STATE_WAIT;
        pthread_mutex_unlock(&a->lock);
    } else if (identifier) {
        target = registry_find_retained(identifier);
        if (!target) return;
    } else {
        return;
    }
    activity_fire_run(target);
    if (target != activity) xpc_release((xpc_object_t)target);
}

void
xpc_activity_list(const char *identifier, dispatch_queue_t queue)
{
    (void)identifier;
    (void)queue;
}

void
xpc_activity_debug(const char *identifier, uint64_t flags,
    dispatch_queue_t queue)
{
    (void)identifier;
    (void)flags;
    (void)queue;
}