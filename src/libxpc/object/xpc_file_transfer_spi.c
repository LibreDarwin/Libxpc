#include <sys/types.h>
#include <mach/mach.h>
#include "xpc_internal.h"

void
xpc_file_transfer_cancel(xpc_object_t xfer)
{
    (void)xfer;
}

void
xpc_file_transfer_copy_io(xpc_object_t xfer, void *io)
{
    (void)xfer; (void)io;
}

xpc_object_t
xpc_file_transfer_create_with_fd(int fd)
{
    (void)fd;
    return NULL;
}

xpc_object_t
xpc_file_transfer_create_with_path(const char *path)
{
    (void)path;
    return NULL;
}

off_t
xpc_file_transfer_get_size(xpc_object_t xfer)
{
    (void)xfer;
    return 0;
}

uint64_t
xpc_file_transfer_get_transfer_id(xpc_object_t xfer)
{
    (void)xfer;
    return 0;
}

void
xpc_file_transfer_send_finished(xpc_object_t xfer, int error)
{
    (void)xfer; (void)error;
}

void
xpc_file_transfer_set_transport_writing_callbacks(xpc_object_t xfer, void *callbacks)
{
    (void)xfer; (void)callbacks;
}

void
xpc_file_transfer_write_finished(xpc_object_t xfer, int error)
{
    (void)xfer; (void)error;
}

off_t
xpc_file_transfer_write_to_fd(xpc_object_t xfer, int fd, off_t offset, size_t length)
{
    (void)xfer; (void)fd; (void)offset; (void)length;
    return -1;
}

off_t
xpc_file_transfer_write_to_path(xpc_object_t xfer, const char *path, off_t offset, size_t length)
{
    (void)xfer; (void)path; (void)offset; (void)length;
    return -1;
}
