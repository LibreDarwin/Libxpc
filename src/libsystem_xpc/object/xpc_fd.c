/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026, LibreDarwin
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
 * xpc_fd.c — file-descriptor values for the reimplemented XPC framework.
 *
 * Apple never puts a raw descriptor number on the wire.  xpc_fd_create()
 * boxes the fd with fileport_makeport(2) into a fileport send right, the
 * serializer parks that port in the message's OOL_PORTS descriptor table,
 * and the receiver unboxes it with fileport_makefd(2).  Each unbox mints a
 * fresh descriptor over the same open file description, which is what lets
 * the sender close(2) its copy as soon as xpc_dictionary_set_fd() returns.
 *
 * There is no borrowing form of xpc_fd_create() (unlike xpc_mach_send_create),
 * so an fd value always owns its fileport right and always deallocates it on
 * release -- whether the right came from fileport_makeport() locally or from
 * the descriptor table of a received message.
 */

#include <sys/fileport.h>

#include "xpc_internal.h"

xpc_object_t
xpc_fd_create_from_port(mach_port_t port)
{
    xpc_fd_t *f = XPC_CAST(xpc_fd_t,
        xpc_object_alloc(&_xpc_type_fd, sizeof(*f)));
    if (!f) return NULL;
    f->port = port;
    return (xpc_object_t)f;
}

xpc_object_t
xpc_fd_create(int fd)
{
    /* Apple passes fd == -1 straight through as a null fileport, so the
     * value round-trips back to -1 from xpc_fd_dup(); every other fd it
     * cannot box yields NULL rather than a value. */
    mach_port_t port = MACH_PORT_NULL;
    if (fd != -1 && fileport_makeport(fd, &port) != 0) {
        return NULL;
    }
    return xpc_fd_create_from_port(port);
}

int
xpc_fd_dup(xpc_object_t xfd)
{
    if (!XPC_OBJECT_CHECK(xfd, &_xpc_type_fd)) return -1;
    return fileport_makefd(XPC_CAST(xpc_fd_t, xfd)->port);
}

#pragma mark - Dictionary accessors (declared in xpc.h)

void
xpc_dictionary_set_fd(xpc_object_t dict, const char *key, int fd)
{
    xpc_object_t obj = xpc_fd_create(fd);
    if (!obj) return;
    xpc_dictionary_set_value(dict, key, obj);
    xpc_release(obj);
}

int
xpc_dictionary_dup_fd(xpc_object_t dict, const char *key)
{
    xpc_object_t v = xpc_dictionary_get_value(dict, key);
    if (!v || !XPC_OBJECT_CHECK(v, &_xpc_type_fd)) return -1;
    return xpc_fd_dup(v);
}
