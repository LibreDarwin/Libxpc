#include <stdint.h>
#include <sys/types.h>
#include "xpc_internal.h"

void
xpc_event_publisher_activate(xpc_object_t publisher)
{
    (void)publisher;
}

xpc_object_t
xpc_event_publisher_copy_event(xpc_object_t publisher)
{
    (void)publisher;
    return NULL;
}

xpc_object_t
xpc_event_publisher_create(const char *name)
{
    (void)name;
    return NULL;
}

xpc_object_t
xpc_event_publisher_create_subscription(xpc_object_t publisher)
{
    (void)publisher;
    return NULL;
}

void
xpc_event_publisher_fire(xpc_object_t publisher, xpc_object_t event)
{
    (void)publisher; (void)event;
}

void
xpc_event_publisher_fire_barrier(xpc_object_t publisher)
{
    (void)publisher;
}

void
xpc_event_publisher_fire_noboost(xpc_object_t publisher, xpc_object_t event)
{
    (void)publisher; (void)event;
}

void
xpc_event_publisher_fire_with_reply(xpc_object_t publisher, xpc_object_t event, xpc_object_t reply)
{
    (void)publisher; (void)event; (void)reply;
}

xpc_object_t
xpc_event_publisher_fire_with_reply_sync(xpc_object_t publisher, xpc_object_t event)
{
    (void)publisher; (void)event;
    return NULL;
}

uint32_t
xpc_event_publisher_get_subscriber_asid(xpc_object_t publisher)
{
    (void)publisher;
    return 0;
}

void
xpc_event_publisher_set_error_handler(xpc_object_t publisher, xpc_handler_t handler)
{
    (void)publisher; (void)handler;
}

void
xpc_event_publisher_set_event(xpc_object_t publisher, xpc_object_t event)
{
    (void)publisher; (void)event;
}

void
xpc_event_publisher_set_handler(xpc_object_t publisher, xpc_handler_t handler)
{
    (void)publisher; (void)handler;
}

void
xpc_event_publisher_set_initial_load_completed_handler_4remoted(xpc_object_t publisher, xpc_handler_t handler)
{
    (void)publisher; (void)handler;
}

void
xpc_event_publisher_set_subscriber_keepalive(xpc_object_t publisher, xpc_object_t keepalive)
{
    (void)publisher; (void)keepalive;
}

void
xpc_event_publisher_set_throttling(xpc_object_t publisher, uint64_t throttle_ms)
{
    (void)publisher; (void)throttle_ms;
}
