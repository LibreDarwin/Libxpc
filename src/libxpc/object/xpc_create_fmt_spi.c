#include "xpc_internal.h"
#include <stdarg.h>

__attribute__((__format__(printf, 1, 2)))
xpc_object_t
xpc_create_with_format(const char *fmt, ...)
{
    (void)fmt;
    va_list ap;
    va_start(ap, fmt);
    va_end(ap);
    return NULL;
}

xpc_object_t
xpc_create_with_format_and_arguments(const char *fmt, va_list ap)
{
    (void)fmt;
    (void)ap;
    return NULL;
}
