/*
 * Copyright (c) 2026 The xnuports project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "xpc_internal.h"

/*
 * xpc_main(handler) runs the XPC service main loop and never returns.  The
 * process is expected to have been launched as an XPC service, so the
 * listener name comes from launchd's XPC_SERVICE_NAME environment variable.
 * On the first inbound message libxpc calls <handler> with the service
 * connection; the handler installs its own event handler on it to receive
 * that peer's traffic (the opening message is consumed by this bookkeeping,
 * as in the test-connections listener).  Connections are serviced on
 * libxpc's own threads, so main just idles.
 *
 * Apple's libxpc additionally registers launchd jobs, validates _RoleAccount
 * / _AllowedClients from the XPC service dictionary, and mints a dedicated
 * per-peer connection for every inbound client; this port's runtime has no
 * such machinery, so the single listener doubles as the peer conduit and
 * the role checks are omitted.
 */
void
xpc_main(xpc_connection_handler_t handler)
{
    const char *name = getenv("XPC_SERVICE_NAME");
    __block xpc_connection_t service = NULL;
    __block int opened = 0;

    if (name && *name) {
        service = xpc_connection_create_mach_service(name, NULL,
            XPC_CONNECTION_MACH_SERVICE_LISTENER);
        if (service) {
            xpc_connection_set_event_handler(service, ^(xpc_object_t event) {
                if (opened) return;
                if (xpc_get_type(event) == &_xpc_type_error) return;
                opened = 1;
                handler(service);
            });
            xpc_connection_activate(service);
        }
    }

    /* Never returns.  An idle main queue must not let xpc_main drain, so
     * dispatch_main() defers to an unconditional sleep if it ever returns. */
    dispatch_main();
    for (;;) sleep(INT_MAX);
}