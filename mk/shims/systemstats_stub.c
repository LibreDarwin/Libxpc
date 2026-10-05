/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 *
 * systemstats_boot() stub.  See mk/shims/systemstats/systemstats.h for why the
 * real systemstats framework is unavailable.  launchctl calls this once at
 * startup to cache the vm-pressure sysctl OID; with no framework to prime, the
 * call is a no-op.
 */

void
systemstats_boot(void)
{
}
