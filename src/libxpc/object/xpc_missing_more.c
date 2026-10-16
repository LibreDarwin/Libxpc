#include "../include/xpc_private.h"
#include "xpc_internal.h"
#include <stdint.h>
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <pthread.h>

void xpc_connection_set_logging(xpc_connection_t c, int level) { (void)c; (void)level; }

void __xpc_connection_set_logging(xpc_connection_t c, int level) { (void)c; (void)level; }
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

bool _availability_version_check(unsigned int platform, unsigned int *versions)
{
    (void)platform; (void)versions;
    return true;
}

int _launch_job_routine(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

int _launch_job_routine_async(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

int _launch_job_query_routine(void *job, void *resp, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)job; (void)resp; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    return 0;
}

xpc_object_t _xpc_bool_create_distinct(bool v)
{
    return xpc_bool_create(v);
}

void _xpc_bool_set_value(xpc_object_t b, bool v)
{
    xpc_bool_set_value(b, v);
}

xpc_object_t _xpc_connection_get_parent_4test(xpc_object_t c)
{
    (void)c;
    return NULL;
}

mach_port_t _xpc_connection_get_recvp_4test(xpc_object_t c)
{
    (void)c;
    return MACH_PORT_NULL;
}

void _xpc_connection_set_event_handler_f(xpc_connection_t c, void *h)
{
    (void)c; (void)h;
}

void _xpc_data_set_value(xpc_object_t obj, const void *bytes, size_t length)
{
    xpc_data_set_value(obj, bytes, length);
}

void _xpc_double_set_value(xpc_object_t obj, double value)
{
    xpc_double_set_value(obj, value);
}

void _xpc_int64_set_value(xpc_object_t obj, int64_t value)
{
    xpc_int64_set_value(obj, value);
}

const char *_xpc_error_key_description(void)
{
    return "description";
}

const char *_xpc_event_key_name(void)
{
    return "name";
}

const char *_xpc_event_key_stream_name(void)
{
    return "stream";
}

mach_port_t _xpc_fd_get_port(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_fd)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_fd_t, obj)->port;
}

int _xpc_pipe_interface_routine_async(void *a1, void *a2, void *a3, void *a4, void *a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -1;
}

int _xpc_pipe_interface_simpleroutine(void *a1, void *a2, void *a3, void *a4)
{
    (void)a1; (void)a2; (void)a3; (void)a4;
    return -1;
}

xpc_connection_t _xpc_connection_create_internal_listener(const char *name, uint64_t flags)
{
    (void)flags;
    xpc_connection_t c = xpc_connection_create_mach_service(name, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!c) return NULL;
    struct _xpc_connection_s *conn = (struct _xpc_connection_s *)c;
    conn->have_peer_audit = true;
    memset(&conn->peer_audit, 0, sizeof(conn->peer_audit));
    return c;
}

const void *_xpc_type_base(void) { return NULL; }
const void *_xpc_type_bundle(void) { return NULL; }
const void *_xpc_type_file_transfer(void) { return NULL; }
const void *_xpc_type_pipe(void) { return NULL; }
const void *_xpc_type_serializer(void) { return NULL; }
const void *_xpc_type_string_cache(void) { return NULL; }
const void *_xpc_vtables(void) { return NULL; }

const void *_xpc_runtime_get_entitlements_data(void) { return NULL; }
const void *_xpc_runtime_get_self_entitlements(void) { return NULL; }

/*
 * _xpc_runtime_is_app_sandboxed() — Apple caches the result of the
 * containerization check in a process-global.  A process is app-sandboxed
 * when it was spawned into an App Sandbox container, which the kernel
 * exposes via the APP_SANDBOX_CONTAINER_ID environment variable.
 */
bool _xpc_runtime_is_app_sandboxed(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = (getenv("APP_SANDBOX_CONTAINER_ID") != NULL) ? 1 : 0;
    }
    return cached != 0;
}

void _xpc_runtime_process_has_entered_sandbox(void) { }

int _xpc_service_routine(void *a, void *b, void *c, void *d, void *e, void *f)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return 0;
}

xpc_object_t _xpc_session_create_from_connection_4SWIFT(xpc_connection_t conn,
    const char *name, uint64_t flags, xpc_rich_error_t *error_out)
{
    (void)name;
    (void)flags;
    if (error_out) *error_out = NULL;
    if (!conn) return NULL;
    struct _xpc_session_s *s = (struct _xpc_session_s *)(void *)
        xpc_object_alloc(&_xpc_type_session, sizeof(*s));
    if (!s) return NULL;
    s->connection = (xpc_connection_t)(void *)xpc_retain((xpc_object_t)(void *)conn);
    return (xpc_object_t)(void *)s;
}

void *_xpc_session_extract_connection_4SWIFT(void *sess)
{
    if (!XPC_OBJECT_CHECK(sess, &_xpc_type_session)) return NULL;
    struct _xpc_session_s *s = (struct _xpc_session_s *)sess;
    xpc_connection_t c = s->connection;
    s->connection = NULL;
    return c;
}

bool _xpc_session_get_peer_audit_token_4SWIFT(audit_token_t *token, void *sess)
{
    if (token) memset(token, 0, sizeof(*token));
    if (!token || !XPC_OBJECT_CHECK(sess, &_xpc_type_session)) return false;
    xpc_connection_t c = ((struct _xpc_session_s *)sess)->connection;
    if (!c) return false;
    struct _xpc_connection_s *conn = (struct _xpc_connection_s *)(void *)c;
    pthread_mutex_lock(&conn->lock);
    bool have = conn->have_peer_audit;
    if (have) *token = conn->peer_audit;
    pthread_mutex_unlock(&conn->lock);
    return have;
}

mach_port_t _xpc_shmem_get_mach_port(void *sh)
{
    if (!XPC_OBJECT_CHECK(sh, &_xpc_type_shmem)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_shmem_t, sh)->port;
}

void _xpc_string_set_value(xpc_object_t o, const char *v)
{
    xpc_string_set_value(o, v);
}

void _xpc_domain_routine(void *a, void *b, void *c)
{
    (void)a; (void)b; (void)c;
}

// Minimal launch_data/bootstrap/vproc stubs to match exports
#include <stdlib.h>


xpc_object_t _xpc_payload_create_from_mach_msg(void *param_1, unsigned long param_2)
{
	(void)param_1;
	(void)param_2;
	return xpc_dictionary_create(NULL, NULL, 0);
}
#include <stdbool.h>

bool _xpc_peer_requirement_match_token(xpc_object_t req, xpc_object_t token)
{
	(void)req; (void)token;
	return false;
}

void _xpc_service_last_xref_cancel(void *service)
{
	(void)service;
}
#include <mach/mach.h>

mach_msg_return_t _xpc_pipe_handle_mig(mach_msg_header_t *msg, mach_msg_header_t *reply)
{
	(void)msg; (void)reply;
	return MACH_MSG_SUCCESS;
}
int _launch_msg2(void) { return 0; }
int _launch_server_test_routine(void) { return 0; }
void* _launch_service_stats_copy_impl(void) { return NULL; }
void* _launch_service_stats_copy_4ppse_impl(void) { return NULL; }
int _launch_trial_factors_routine(void) { return 0; }
void __vprocmgr_log_forward(void *r, int l) { (void)r; (void)l; }
int __vprocmgr_move_subset_to_user(void *a,void *b,void *c) { (void)a;(void)b;(void)c; return 0; }
int __vprocmgr_switch_to_session(void *s, void *t) { (void)s;(void)t; return 0; }
kern_return_t _bootstrap_check_in(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_check_in2(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_check_in3(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_create_server(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_create_service(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_get_root(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_info(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_init(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_look_up(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_look_up2(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_look_up3(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_look_up_per_user(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_lookup_children(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_parent(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_register(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_register2(void) { return KERN_SUCCESS; }
kern_return_t _bootstrap_status(void) { return KERN_SUCCESS; }
