#include "xpc_internal.h"
#include <stdarg.h>

__attribute__((__format__(printf, 2, 3)))
xpc_object_t
xpc_create_reply_with_format(xpc_object_t original, const char *fmt, ...)
{
    (void)original;
    (void)fmt;
    va_list ap;
    va_start(ap, fmt);
    va_end(ap);
    return NULL;
}

xpc_object_t
xpc_create_reply_with_format_and_arguments(xpc_object_t original, const char *fmt, va_list ap)
{
    (void)original;
    (void)fmt;
    (void)ap;
    return NULL;
}
