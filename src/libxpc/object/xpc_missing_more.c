#include "xpc_internal.h"
#include <stdint.h>
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>

void xpc_connection_set_logging(xpc_connection_t c, int level) { (void)c; (void)level; }

void ___xpc_connection_set_logging(xpc_connection_t c, int level) { (void)c; (void)level; }
xpc_object_t xpc_payload_create_from_mach_msg(void *msg, size_t size, int *err) { (void)msg; (void)size; if (err) *err = 0; return xpc_dictionary_create(NULL, NULL, 0); }
bool xpc_peer_requirement_match_token(xpc_object_t req, void *token) { (void)req; if (token == NULL) return true; return false; }
kern_return_t xpc_pipe_handle_mig(xpc_pipe_t p, void *msg, int *err) { (void)p; (void)msg; if (err) *err = -1; return KERN_FAILURE; }
void xpc_service_last_xref_cancel(void) { }
int availability_version_check(int *ver) { if (ver) *ver = 0; return -1; }

uint64_t os_system_version_get_current_version(void) {
    char buf[128];
    size_t sz = sizeof(buf);
    if (sysctlbyname("kern.osproductversion", buf, &sz, NULL, 0) == 0) {
        int maj=0,min=0,pat=0;
        sscanf(buf,"%d.%d.%d",&maj,&min,&pat);
        return ((uint64_t)maj<<16) | ((uint64_t)min<<8) | (uint64_t)pat;
    }
    return 0x150000;
}

/* amfi helpers - minimal stubs */
int amfi_developer_mode_resolved(void) { return 0; }
int amfi_developer_mode_status(void) { return 0; }
int amfi_interface_authorize_local_signing(void) { return -1; }
int amfi_interface_cdhash_in_trustcache(void) { return -1; }
int amfi_interface_get_local_signing_private_key(void) { return -1; }
int amfi_interface_get_local_signing_public_key(void) { return -1; }
int amfi_interface_query_bootarg_state(void) { return -1; }
int amfi_interface_set_local_signing_public_key(void) { return -1; }
int amfi_launch_constraint_matches_process(void) { return 0; }
int amfi_launch_constraint_set_spawnattr(void) { return -1; }
int amfi_restricted_execution_mode_enable(void) { return -1; }
int amfi_restricted_execution_mode_status(void) { return 0; }

bool __availability_version_check(unsigned int platform, unsigned int *versions)
{
    (void)platform; (void)versions;
    return true;
}

int __launch_job_routine(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

int __launch_job_routine_async(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

int __launch_job_query_routine(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

xpc_object_t __xpc_bool_create_distinct(bool v)
{
    return xpc_bool_create(v);
}

void __xpc_bool_set_value(xpc_object_t b, bool v)
{
    (void)b; (void)v;
}

xpc_object_t __xpc_connection_create_internal_listener(const char *name, uint64_t flags)
{
    (void)name; (void)flags;
    return NULL;
}

xpc_object_t __xpc_connection_get_parent_4test(xpc_object_t c)
{
    (void)c;
    return NULL;
}

mach_port_t __xpc_connection_get_recvp_4test(xpc_object_t c)
{
    (void)c;
    return MACH_PORT_NULL;
}

void __xpc_connection_set_event_handler_f(xpc_connection_t c, void *h)
{
    (void)c; (void)h;
}
