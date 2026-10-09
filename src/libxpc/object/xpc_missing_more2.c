#include "xpc_internal.h"
#include <stdint.h>
#include <mach/mach.h>

void os_transaction_log_active(void) {}
char *os_transaction_get_description(void *t) { (void)t; return NULL; }
void *os_transaction_copy_description(void *t) { (void)t; return NULL; }
void *os_transaction_create(const char *desc) { (void)desc; return NULL; }
uint64_t os_transaction_get_ra(void *t) { (void)t; return 0; }
uint64_t os_transaction_get_timestamp(void *t) { (void)t; return 0; }
void os_transaction_needs_more_time(void) {}

void place_hold_on_real_loginwindow(int h) { (void)h; }

void xpc_install_remote_hooks(void *h) { (void)h; }

uint64_t os_system_version_get_ios_support_version(void) { return 0; }
uint64_t os_system_version_sim_get_current_host_version(void) { return 0; }
