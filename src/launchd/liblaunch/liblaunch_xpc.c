#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <xpc/xpc.h>
#include <xpc/private.h>
#include <xpc/launchd.h>

#include "launch_internal.h"
#include "vproc.h"
#include "vproc_priv.h"

extern int _xpc_errno_set(int);

static xpc_object_t
launch_xpc_msg_to_dict(const launch_data_t msg)
{
	xpc_object_t dict = xpc_dictionary_create_empty();
	if (msg == NULL) return dict;
	launch_data_type_t t = launch_data_get_type(msg);
	if (t == LAUNCH_DATA_DICTIONARY) {
		launch_data_dict_iter_t iter;
		launch_data_t val;
		const char *key;
		launch_data_dict_iterate(msg, &iter);
		while ((key = launch_data_dict_next(msg, &iter))) {
			val = launch_data_dict_lookup(msg, key);
			launch_data_type_t vt = launch_data_get_type(val);
			if (vt == LAUNCH_DATA_STRING)
				xpc_dictionary_set_string(dict, key, launch_data_get_string(val));
			else if (vt == LAUNCH_DATA_INTEGER)
				xpc_dictionary_set_int64(dict, key, launch_data_get_integer(val));
			else if (vt == LAUNCH_DATA_BOOL)
				xpc_dictionary_set_bool(dict, key, launch_data_get_bool(val));
			else if (vt == LAUNCH_DATA_REAL)
				xpc_dictionary_set_double(dict, key, launch_data_get_real(val));
			else if (vt == LAUNCH_DATA_ARRAY) {
				size_t n = launch_data_array_get_count(val);
				xpc_object_t xa = xpc_array_create_empty();
				for (size_t i = 0; i < n; i++) {
					launch_data_t ae = launch_data_array_get_index(val, i);
					if (launch_data_get_type(ae) == LAUNCH_DATA_STRING)
						xpc_array_set_string(xa, XPC_ARRAY_APPEND, launch_data_get_string(ae));
					else if (launch_data_get_type(ae) == LAUNCH_DATA_INTEGER)
						xpc_array_set_int64(xa, XPC_ARRAY_APPEND, launch_data_get_integer(ae));
					else if (launch_data_get_type(ae) == LAUNCH_DATA_BOOL)
						xpc_array_set_bool(xa, XPC_ARRAY_APPEND, launch_data_get_bool(ae));
				}
				xpc_dictionary_set_value(dict, key, xa);
				xpc_release(xa);
			} else if (vt == LAUNCH_DATA_DICTIONARY) {
				xpc_object_t sd = launch_xpc_msg_to_dict(val);
				xpc_dictionary_set_value(dict, key, sd);
				xpc_release(sd);
			}
		}
	}
	return dict;
}

static launch_data_t
xpc_dict_to_launch_data(xpc_object_t dict)
{
	launch_data_t resp = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
	xpc_object_t keys = xpc_dictionary_get_keys(dict);
	size_t nk = xpc_array_get_count(keys);
	for (size_t i = 0; i < nk; i++) {
		const char *k = xpc_array_get_string(keys, i);
		xpc_object_t v = xpc_dictionary_get_value(dict, k);
		if (!v) continue;
		xpc_type_t vt = xpc_get_type(v);
		if (vt == XPC_TYPE_STRING)
			launch_data_dict_insert(resp, launch_data_new_string(xpc_string_get_string_ptr(v)), k);
		else if (vt == XPC_TYPE_INT64)
			launch_data_dict_insert(resp, launch_data_new_integer(xpc_int64_get_value(v)), k);
		else if (vt == XPC_TYPE_BOOL)
			launch_data_dict_insert(resp, launch_data_new_bool(xpc_bool_get_value(v)), k);
		else if (vt == XPC_TYPE_DOUBLE)
			launch_data_dict_insert(resp, launch_data_new_real(xpc_double_get_value(v)), k);
		else if (vt == XPC_TYPE_ARRAY) {
			size_t na = xpc_array_get_count(v);
			launch_data_t la = launch_data_alloc(LAUNCH_DATA_ARRAY);
			for (size_t j = 0; j < na; j++) {
				xpc_object_t ae = xpc_array_get_value(v, j);
				xpc_type_t aet = xpc_get_type(ae);
				if (aet == XPC_TYPE_STRING)
					launch_data_array_set_index(la, launch_data_new_string(xpc_string_get_string_ptr(ae)), j);
				else if (aet == XPC_TYPE_INT64)
					launch_data_array_set_index(la, launch_data_new_integer(xpc_int64_get_value(ae)), j);
				else if (aet == XPC_TYPE_BOOL)
					launch_data_array_set_index(la, launch_data_new_bool(xpc_bool_get_value(ae)), j);
			}
			launch_data_dict_insert(resp, la, k);
		} else if (vt == XPC_TYPE_DICTIONARY) {
			launch_data_dict_insert(resp, xpc_dict_to_launch_data(v), k);
		}
	}
	return resp;
}

launch_data_t
launch_msg_xpc(const launch_data_t request, launch_data_t *out_response, const char *session)
{
	(void)session;
	xpc_object_t req = launch_xpc_msg_to_dict(request);
	xpc_object_t rep = NULL;
	const char *verb = launch_data_get_string(launch_data_dict_lookup(request, LAUNCH_KEY_SUBMITJOB));
	uint64_t routine = 3;
	int64_t type = 0, handle = 0, domain_port = 0;
	xpc_object_t dport = NULL;
	bool legacy = true;
	if (launch_data_dict_lookup(request, LAUNCHD_XPC_TYPE_KEY))
		type = launch_data_get_integer(launch_data_dict_lookup(request, LAUNCHD_XPC_TYPE_KEY));
	if (launch_data_dict_lookup(request, LAUNCHD_XPC_HANDLE_KEY))
		handle = launch_data_get_integer(launch_data_dict_lookup(request, LAUNCHD_XPC_HANDLE_KEY));
	if (launch_data_dict_lookup(request, LAUNCHD_XPC_DOMAIN_PORT_KEY)) {
		domain_port = launch_data_get_integer(launch_data_dict_lookup(request, LAUNCHD_XPC_DOMAIN_PORT_KEY));
		dport = xpc_mach_send_create((mach_port_t)domain_port);
	}
	if (launch_data_dict_lookup(request, LAUNCHD_XPC_LEGACY_KEY))
		legacy = launch_data_get_bool(launch_data_dict_lookup(request, LAUNCHD_XPC_LEGACY_KEY));
	xpc_object_t xreq = xpc_dictionary_create_empty();
	if (dport) {
		xpc_dictionary_set_value(xreq, LAUNCHD_XPC_DOMAIN_PORT_KEY, dport);
		xpc_release(dport);
	}
	xpc_dictionary_set_int64(xreq, LAUNCHD_XPC_TYPE_KEY, type ? type : 7);
	xpc_dictionary_set_int64(xreq, LAUNCHD_XPC_HANDLE_KEY, handle ? handle : 0);
	xpc_dictionary_set_bool(xreq, LAUNCHD_XPC_LEGACY_KEY, legacy);
	if (launch_data_dict_lookup(request, "launchd_xpc_bundle"))
		xpc_dictionary_set_string(xreq, "launchd_xpc_bundle", launch_data_get_string(launch_data_dict_lookup(request, "launchd_xpc_bundle")));
	if (req) {
		if (verb) {
			xpc_object_t job = xpc_dictionary_create_empty();
			xpc_dictionary_set_value(job, verb, req);
			xpc_dictionary_set_value(xreq, LAUNCH_KEY_SUBMITJOB, job);
			xpc_release(job);
		} else {
			xpc_dictionary_set_value(xreq, "request", req);
		}
	}
	rep = xpc_launchd_routine((xpc_object_t)routine, xreq);
	xpc_release(xreq);
	if (req) xpc_release(req);
	if (rep == NULL) { errno = EIO; return NULL; }
	launch_data_t r = xpc_dict_to_launch_data(rep);
	xpc_release(rep);
	int64_t err = 0;
	if (launch_data_dict_lookup(r, LAUNCH_KEY_ERROR)) err = launch_data_get_integer(launch_data_dict_lookup(r, LAUNCH_KEY_ERROR));
	else if (launch_data_dict_lookup(r, "error")) err = launch_data_get_integer(launch_data_dict_lookup(r, "error"));
	if (err) {
		if (err == 113) err = ESRCH;
		errno = (int)err;
		if (out_response) *out_response = NULL;
		launch_data_free(r);
		return NULL;
	}
	if (out_response) *out_response = r;
	return r;
}
