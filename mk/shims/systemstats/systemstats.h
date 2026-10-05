/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 *
 * Shim for Darwin's closed-source <systemstats/systemstats.h>.
 *
 * Apple's launchctl includes this header and calls exactly one entry point,
 * systemstats_boot(), which primes the kern.memorystatus_vm_pressure_levels
 * sysctl OID cache.  The framework is not part of LibDarwin and no SDK carries
 * the header, so we declare just that symbol and provide the definition in
 * systemstats_stub.c.
 *
 * Consequence: launchctl will not observe vm-pressure transitions from this
 * header alone.  Everything else about the command's behaviour is unaffected.
 */

#ifndef _SYSTEMSTATS_SHIM_H_
#define _SYSTEMSTATS_SHIM_H_

void systemstats_boot(void);

#endif /* _SYSTEMSTATS_SHIM_H_ */
