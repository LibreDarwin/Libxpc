#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (C) 2026 Sunneva N. Mariu
#
"""
Generate mk/patches/launchd/0004-launchctl-exusage.patch by line surgery on
apple-oss launchctl.c that already carries 0001, 0002 and 0003.

/bin/launchctl signals a bad command line with 64, not 1:

    $ /bin/launchctl getenv; echo $?
    Usage: launchctl getenv <key>
    64

64 is EX_USAGE from <sysexits.h>, which launchctl.c already includes (line
81), so no new header is needed -- only the constant.  Apple's published
source spells every one of these sites `return 1;`, so this is a gap in the
open-source tree rather than something wrong with our build.

Two sites are deliberately *not* touched, because matching Apple's exit
status there means leaving the existing status alone:

  - debug_cmd's usage check.  /bin/launchctl's privilege test runs first and
    answers "This subcommand requires root privileges: debug" with 1 for every
    argument count, so its usage branch is unreachable from an unprivileged
    process and its status cannot be observed.  Returning 64 there would make
    ours disagree with Apple where today it agrees.

  - demux_cmd's unknown-subcommand path, which is a different error entirely:
    /bin/launchctl prints "Unrecognized subcommand: X" and returns 1 for both
    a typo'd subcommand and, as it happens, the ~21 subcommands Apple's
    published source omits.  64 is reserved for a recognised subcommand that
    was given bad arguments.

help_cmd gets the one behavioural change: /bin/launchctl answers
"help nosuchthing" with 64 and "Usage: launchctl help <subcommand>", where the
published source ignores argv entirely and returns 0 for any argument.  A
recognised subcommand still falls through to the existing table, because
per-subcommand help text is not in this tree and printing the table is more
useful than printing nothing.
"""

import sys

src, dst = sys.argv[1], sys.argv[2]

with open(src, "r") as f:
    lines = f.readlines()

USAGE_RE = r'launchctl_log\(LOG_ERR,\s*"[^"]*[Uu]sage'

# (1-based line in the 0001+0002+0003 source, command) for every usage site
# except debug_cmd.  The numbers are an audit aid: the asserts below fail
# loudly if Apple's tree moves them, so a stale entry cannot silently patch
# the wrong branch.
SKIP_DEBUG = 'usage: debug <label> <value>'


def is_debug_site(window):
    return any(SKIP_DEBUG in line for line in window)


# --- Pass 1: usage errors return EX_USAGE -------------------------------
# A usage site's "return 1;" may sit one line below the message, or below the
# multi-line limit_cmd message (three log calls).  Scan a bounded window and
# stop at the first return, so only the usage branch is rewritten.
out = []
converted = []
i = 0
while i < len(lines):
    line = lines[i]
    if 'launchctl_log(LOG_ERR, "' in line and (
        'usage: ' in line or 'Usage: ' in line
    ):
        window = lines[i:i + 5]
        hit = next(
            (j for j, cand in enumerate(window) if cand.strip() == "return 1;"),
            None,
        )
        if hit is not None:
            if is_debug_site(window):
                out.extend(window[:hit + 1])
            else:
                out.extend(window[:hit])
                # Keep the original return's indentation: submit's usage site
                # sits inside a switch case and is one tab deeper than the rest.
                indent = window[hit][:len(window[hit]) - len(window[hit].lstrip())]
                out.append(indent + "return EX_USAGE;\n")
                converted.append(i + 1)
            i += hit + 1
            continue
    out.append(line)
    i += 1

assert len(converted) == 16, f"expected 16 usage sites, got {len(converted)}"
lines = out

# --- Pass 2: help_cmd rejects an unknown subcommand --------------------
# Inserted between the level fixup and the command table, so `i` is already
# declared and the two existing loops that compute and print the table are
# untouched.  A recognised name falls straight through to them.
HELP = '''\tif (argc > 1) {
\t\tbool found = false;

\t\tfor (i = 0; i < (sizeof cmds / sizeof cmds[0]); i++) {
\t\t\tif (!strcmp(cmds[i].name, argv[1])) {
\t\t\t\tfound = true;
\t\t\t\tbreak;
\t\t\t}
\t\t}
\t\tif (!found) {
\t\t\tlaunchctl_log(LOG_ERR, "usage: %s help <subcommand>", getprogname());
\t\t\treturn EX_USAGE;
\t\t}
\t}
'''

out = []
seen = 0
for line in lines:
    if (
        seen == 0
        and line.rstrip("\n")
        == '\tlaunchctl_log(level, "usage: %s <subcommand>", getprogname());'
    ):
        seen += 1
        out.append(line)
        out.append("\n")
        out.append(HELP)
        continue
    out.append(line)
assert seen == 1, f"expected one help_cmd usage line, got {seen}"

with open(dst, "w") as f:
    f.writelines(out)

print(f"wrote {dst}: {len(converted)} usage sites -> EX_USAGE, help_cmd validated")
