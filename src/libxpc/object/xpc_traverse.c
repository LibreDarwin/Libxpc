#include "xpc_internal.h"

void xpc_traverse_serialized_data(const void *data, size_t len, void *ctx,
    void (*cb)(const void *, size_t, void *))
{
    (void)data; (void)len; (void)ctx; (void)cb;
}
