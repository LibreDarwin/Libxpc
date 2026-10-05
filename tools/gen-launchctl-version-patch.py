#!/usr/bin/env python3
"""Generate mk/patches/launchd/0003-*.patch by line surgery on apple-oss
launchctl.c that already carries 0001 and 0002.

Unlike 0001, Apple's open-source launchctl has no "version" subcommand:
the string is absent from launchctl.c, so it exits "unknown subcommand"
where /bin/launchctl answers.  /bin/launchctl bakes in its *own* banner

    Darwin Bootstrapper Control Interface Version 7.0.0: ...; .../launchctl/...

and "launchctl version" prints a *different* one, describing launchd
(19:58:40, .../launchd/ rather than 20:03:23, .../launchctl/), which
means it is fetched from launchd at run time.  That banner lives in code
Apple does not publish -- it is in neither Apple's launchd tree nor any
SDK -- so there is nothing upstream to copy.  We compile our own in,
sourced from Info.plist and git, which for a single-tree build describes
the same bootstrapper the command is asking about.

The macro is not passed on the command line: the banner contains spaces
and has to survive both bmake and a shell, which -D quoting does not
manage.  The build generates it into the same shim directory that already
carries the CoreFoundation and systemstats shims, and that header is the
single definition site.  The fallback below only covers a shim header
that omits the define -- if the header is missing the #include still
fails, which is the same contract as the other shims.
"""
import re
import sys

src = sys.argv[1]
dst = sys.argv[2]

with open(src, "r") as f:
    lines = f.readlines()

# --- Hunk 1: pull in the build-generated banner --------------------------
out = []
seen = 0
for line in lines:
    out.append(line)
    if line.rstrip("\n") == '#include "xpc_private.h"':
        seen += 1
        out.append("\n")
        out.append('#include "xpc_build_version.h" /* generated: this build\'s banner */\n')
        out.append("#ifndef XPC_BOOTSTRAP_VERSION\n")
        out.append('#define XPC_BOOTSTRAP_VERSION "Darwin Bootstrapper Version (unknown build)"\n')
        out.append("#endif\n")
assert seen == 1, f"expected exactly one xpc_private.h include, got {seen}"
lines = out

# --- Hunk 2: forward declaration -----------------------------------------
# Anchor on 0002's own declaration, which is unique and already present,
# rather than help_cmd the way 0001 did: help_cmd's successor differs once
# 0001 and 0002 have both inserted below it.
out = []
seen = 0
for line in lines:
    out.append(line)
    if line.rstrip("\n") == "static int attach_cmd(int argc, char *const argv[]);":
        seen += 1
        out.append("static int version_cmd(int argc, char *const argv[]);\n")
assert seen == 1, f"expected one attach_cmd declaration, got {seen}"
lines = out

# --- Hunk 3: cmds[] entry ------------------------------------------------
# Copy the separator runs from the sibling row so the patch bytes match
# the file's real tab layout, the same trick 0001 uses on the list row.
ATTACH_RE = re.compile(r'^(\t*)\{ "attach",(\t+)attach_cmd,(\t+)".*')

out = []
seen = 0
for line in lines:
    m = ATTACH_RE.match(line)
    if m and seen == 0:
        seen += 1
        lead, sep1, sep2 = m.group(1), m.group(2), m.group(3)
        out.append(lead + '{ "version",' + sep1 + 'version_cmd,' + sep2 + \
            '"Print the bootstrapper version." },\n')
    out.append(line)
assert seen == 1, f"expected one attach cmds[] row, got {seen}"
lines = out

# --- Hunk 4: definition --------------------------------------------------
# Insert immediately *before* the "static int" that introduces attach_cmd's
# definition, so that "static int" stays bound to attach_cmd.  0001's bug
# was inserting after a bare return type and orphaning it; anchoring on the
# two-line sequence and emitting a self-contained definition cannot repeat
# that.  The same for version_cmd is emitted whole, so nothing dangles.
FUNC = (
    "static int\n"
    "version_cmd(int argc __attribute__((unused)), char * const argv[] __attribute__((unused)))\n"
    "{\n"
    "\tfprintf(stdout, \"%s\\n\", XPC_BOOTSTRAP_VERSION);\n"
    "\treturn 0;\n"
    "}\n"
    "\n"
)

out = []
seen = 0
for i, line in enumerate(lines):
    if (seen == 0 and line.rstrip("\n") == "static int"
            and i + 1 < len(lines)
            and lines[i + 1].startswith("attach_cmd(")):
        seen += 1
        if out and out[-1].strip() != "":
            out.append("\n")
        out.append(FUNC)
    out.append(line)
assert seen == 1, f"expected one attach_cmd definition, got {seen}"
lines = out

with open(dst, "w") as f:
    f.writelines(lines)

print(f"wrote {dst}")
