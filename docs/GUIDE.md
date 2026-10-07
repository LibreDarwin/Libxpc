# XPC Library Guide

Single guide for this tree, previously split across `HANDBOOK.md` and
`WIRE_FORMAT.md`. Part I is the overview: layout, build, object model and
boundaries. Part II is the byte-level wire-format specification,
reverse-engineered from Apple's libxpc and cross-validated against it.

## Part I — Overview

This tree contains a small, C-based reimplementation of the core XPC object
model and its inline wire representation. It is intentionally independent of
Apple's libxpc implementation, and is shaped like Apple's libSystem family:
the library component lives at `src/libxpc/` (producing `libxpc.dylib`),
with `launchctl` and the launchd test stub as siblings under `src/` and the
XPC.framework umbrella payload (`module.modulemap`, `Info.plist`) folded
directly into `src/libxpc/`.

### Build

Use BSD make (`bmake`):

```sh
bmake release
bmake test
bmake clean
```

All intermediate objects and test executables are written below `build/`.
The component builds `build/release/libxpc.dylib`; the re-export umbrella
is assembled at `build/release/XPC.framework/`.

`libxpc.dylib` also carries liblaunch: launchd-842's `liblaunch.c`,
`libvproc.c` and `libbootstrap.c` with their `job` and `helper` MIG stubs,
the `launch_*`, `vproc_*` and `bootstrap_*` API that modern Darwin ships
inside libxpc. They build from the patched launchd copy with Apple's own
flags, not our `-Werror`. Their private headers come from xcode-tools'
internal SDK, searched after the public SDK; the build finds a built
xcode-tools beside this tree (`../xcode-tools`, or
`../../Developer/xcode-tools` inside LibreDarwin), or takes
`INTERNAL_SDK=<path>`.

`build/release/launchd` is Apple's launchd-842 itself, built from the same
patched copy with the same flags and linked against `libxpc`; `launchd_stub`
stays as the test double `bmake test` drives. Patches 0003 to 0005 in
`mk/patches/launchd/` turn off quarantine, Sandbox and libauditd
(LibreDarwin's kernel has no such policy), fit launchd to a modern xnu and
SDK, and patch out the XPC domain subsystem, whose `domain.defs` Apple
never published. `include/` holds what no SDK carries for these sources:
`xpc/launchd.h` — the routine keys, operations and jetsam bands launchd
serves, a contract our libxpc's client side shares — and the SPI
availability macros. `src/launchd/xpc_launchd.c` is the libxpc SPI only
launchd calls, built into it: `ld2xpc` and `xpc_call_wakeup`.

launchd's entire XPC surface resolves from our `libxpc`: every routeline
and SPI it references (pipe routines, reply contexts, entitlement SPI,
`xpc_fd_create`, and the `__xpc_bool_true` typed global) is in `nm -gU
build/release/libxpc.dylib`.  That closure matters because once `-lxpc`
resolves to our dylib, ld64 no longer consults the SDK's libSystem
re-export for `xpc_*` names — the entitlement pair and
`vproc_swap_integer` therefore have to be exported by us, and are (the
latter deliberately: macOS keeps it in libxpc, not libSystem).  What
remains in `nm -u build/release/launchd` outside our libxpc is the
ordinary libSystem/libc runtime set (blocks, stdio, `_exit`) plus the
`__vproc_mig_*` flat stubs in libvproc, which bind at runtime against
the Mac's installed libxpc and fail closed (see the component Makefile).

The library links its `/usr/lib/system` siblings directly — the same
`LIBRARY_SEARCH_PATHS = $(SDKROOT)/usr/lib/system` line Apple's
Libsystem.xcconfig uses, resolving the libsystem_info / libsystem_notify /
libsystem_trace re-export stubs in the SDK. The remaining libSystem-family
libraries (log, nv, sbuf) have been merged into libSystem on modern Darwin
and are reached through `-lSystem` when features consume them.

The build uses the public header in `src/libxpc/include/xpc.h`, the module
map in `src/libxpc/module.modulemap`, and the framework metadata in
`src/libxpc/Info.plist`. The framework binary is a thin dylib whose only
load command is an `LC_REEXPORT_DYLIB` of our `libxpc.dylib` — the same
shape as Apple's own XPC.framework, which re-exports
`/usr/lib/system/libxpc.dylib`.

### Object model

Every object begins with:

```c
struct _xpc_object_s {
    xpc_type_t isa;
    _Atomic(uint64_t) refs;
};
```

Type descriptors are process-wide singletons. `xpc_get_type()` returns the
object's descriptor, while retain/release use an atomic reference count.
Containers own strong references to their children; replacing or removing a
value releases the old child.

### Values and containers

The core milestone implements null, boolean, signed and unsigned integers,
double, date, data, string, UUID, arrays, and dictionaries. Dictionaries
preserve insertion order. Array and dictionary convenience accessors are
implemented in terms of the typed value constructors. The pipe layer adds
endpoint, mach-send/mach-recv and shared-memory values; the connection layer
adds connections, sessions and listeners.

`xpc_equal()` is structural for scalar and container values. `xpc_hash()` is
FNV-1a over a type-tagged representation. Descriptions are allocated strings
and must be released with `free()`.

### Wire representation

`xpc_wire_serialize()` and `xpc_wire_deserialize()` are private implementation
helpers used by the pipe layer and tests. The format is specified in Part II
and validated against captured Apple libxpc messages.

The message layout is:

1. 24-byte Mach message header.
2. `CPX@` magic, version 5, flags `0xf000`, and a 32-bit body length.
3. A body count followed by dictionary slots or tagged array values.

All integers are little-endian. Keys and variable payloads use four-byte
alignment. Data and strings carry a byte count; strings include their NUL in
that count. Nested arrays and dictionaries carry a body length before their
body, and their body length excludes that length field.

The implementation recognizes the three observed message IDs:

* `0x10000000`: simpleroutine request
* `0x40000000`: routine request
* `0x20000000`: routine reply

### Pipe boundary

`xpc_pipe_create_from_port()` and invalidation are present. Simpleroutine now
performs a Mach send, while routine allocates a receive right, sends with a
`MACH_SEND_MSG | MACH_RCV_MSG` transaction, validates reply ID `0x20000000`,
and deserializes the reply.

### Connection layer

`xpc_connection_create*()` and the session/listener machinery round-trip
through the pipe layer against `launchd_stub` (in-process) and the live
bootstrap port. Activities run on a poke-driven scheduler without libdispatch.
`tools/test-connections.c` is the connection/activity verification target.

### Verification

`tests/test_core.c` constructs a nested dictionary containing scalar, string,
data, array, and nested dictionary values; serializes it; deserializes it; and
asserts structural equality. The standalone `tools/wiredecode` utility remains
the byte-level validator for captured Apple messages.

### Deliberate boundaries

Error objects, XPC dispatch (queue plumbing), bundles, file-transfer values
and Mach-port ownership semantics are outside this slice. They should be
added only after the object/wire contract remains stable and have dedicated
tests for lifecycle, malformed input, and cross-process behavior.

## Part II — Wire Format Specification

The byte-level XPC dictionary wire format, reverse-engineered from Apple's
libxpc on macOS 26.5 (arm64). All multi-byte integers are **little-endian**.

### 1. Mach Message Envelope

Every XPC message sits inside a standard mach message. The first 24 bytes are the mach message header:

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x00 | 4 | msgh_bits | `MACH_MSGH_BITS(remote, local)` with voucher bits |
| 0x04 | 4 | msgh_size | Total message size in bytes |
| 0x08 | 4 | msgh_remote_port | Send/send-once right to remote endpoint |
| 0x0c | 4 | msgh_local_port | Local port (reply port or 0) |
| 0x10 | 4 | msgh_voucher_port | Mach voucher |
| 0x14 | 4 | msgh_id | Message type identifier (see §6) |

#### msgh_bits Conventions

| Value | Meaning |
|-------|---------|
| `0x130013` | simpleroutine — remote=COPY_SEND, local=COPY_SEND, voucher=COPY_SEND |
| `0x131513` | routine — remote=COPY_SEND, local=MAKE_SEND (carries reply port), voucher=COPY_SEND |
| `0x0012` | reply — remote=SEND_ONCE (one-shot reply port), no local/voucher |

---

### 2. XPC Envelope

Immediately after the 24-byte mach header, the XPC payload begins:

| Offset (from msg start) | Size | Field | Value |
|--------------------------|------|-------|-------|
| 0x18 | 4 | magic | `CPX@` (0x40585043 in LE = `43 50 58 40`) |
| 0x1c | 4 | version | `5` (u32 LE) |
| 0x20 | 4 | flags | `0xf000` for dictionary messages |
| 0x24 | 4 | body_len | Byte length of the body (count + slots) |
| 0x28 | 4 | count | Number of top-level key-value slots |

**body_len** = 4 (count field) + total size of all slot data.
Total inline data = 0x2c (28) bytes of header + body_len.
Combined SEND+RECV messages add inline receive space after the body.

---

### 3. Key Encoding

Each key is a NUL-terminated C string, followed by padding to a **4-byte alignment**.

```
key_bytes = strlen(key) + 1    // include NUL
key_size  = (key_bytes + 3) & ~3
```

Key-value pairs are stored **without** any explicit key-length prefix. The decoder scans for the NUL byte, then aligns forward.

---

### 4. Value Types

After each aligned key, the value begins with a 4-byte little-endian type tag:

| Tag | Type | Payload Size | Payload Encoding |
|-----|------|-------------|------------------|
| `0x1000` | NULL | 0 | (none) |
| `0x2000` | BOOL | 4 | u32: `0` = false, `1` = true |
| `0x3000` | INT64 | 8 | i64 LE |
| `0x4000` | UINT64 | 8 | u64 LE |
| `0x5000` | DOUBLE | 8 | f64 LE (IEEE 754) |
| `0x6000` | POINTER | ? | internal — not expected on the wire |
| `0x7000` | DATE | 8 | i64 LE (nanoseconds, typically epoch-anchored) |
| `0x8000` | DATA | 4 + N (aligned) | u32 LE byte count + raw bytes, padded to 4B |
| `0x9000` | STRING | 4 + N (aligned) | u32 LE byte count + NUL-terminated UTF-8, padded to 4B |
| `0xa000` | UUID | 16 | Raw 16-byte UUID |
| `0xb000` | FD | ? | fileport mach port — layout unconfirmed |
| `0xc000` | SHMEM | 8 | tag + u64 LE size (confirmed: probe11 ports message) |
| `0xd000` | MACH_SEND | 0 | **zero-payload**; descriptor slot index in tag low byte (confirmed: probe11 ports message) |
| `0xe000` | ARRAY | 4 + body_len | u32 LE body length + array body (see §5) |
| `0xf000` | DICT | 4 + body_len | u32 LE body length + dict body (see §5) |
| `0x10000` | ERROR | ? | xpc error object — layout unconfirmed |
| `0x11000` | CONNECTION | ? | connection — layout unconfirmed |
| `0x12000` | ENDPOINT | 0 | **zero-payload**: port reference rides in the enclosing mach_msg's port descriptors (confirmed empirically) |
| `0x13000` | SERIALIZER | ? | internal — not expected on the wire |
| `0x14000` | PIPE | ? | xpc pipe — layout unconfirmed |
| `0x15000` | MACH_RECV | ? | receive right — layout unconfirmed |
| `0x16000` | BUNDLE | ? | bundle — layout unconfirmed |
| `0x17000` | SERVICE | ? | service — layout unconfirmed |
| `0x18000` | SERVICE_INSTANCE | ? | service instance — layout unconfirmed |
| `0x19000` | ACTIVITY | ? | activity — layout unconfirmed |
| `0x1a000` | FILE_TRANSFER | ? | file transfer — layout unconfirmed |

The tag is `type_id << 12` where `type_id` runs 0x1..0x1a: `0x1` = null,
`0x2` = bool, `0x3` = int64, … `0xf` = dict, `0x10`+ = extended types.
`0x12000` = **XPC_ENDPOINT** (type_id 0x12): an `xpc_endpoint_t` serialized
into a message — the object itself contributes zero bytes to the OOL blob,
because the endpoint's mach port is delivered via the message's port
descriptors (`MACH_MSG_PORT_DESCRIPTOR` entries in the msgh body). This
matches `xpc_dictionary_set_value(d, "ep", xpc_endpoint_create(conn))` when
the connection is a pure client handle with no live kernel port reference —
the descriptor slot then carries `MACH_PORT_NULL`.

#### Padding Rules

- **DATA**: payload = `4 (len) + align4(N)` where N is the byte count
- **STRING**: payload = `4 (len) + align4(N)` where N is the byte count (including NUL terminator)
- **ARRAY/DICT**: payload = `4 (body_len) + body_len` — no alignment padding on the body itself
- All other types are fixed-size with no padding

#### Total Slot Size (for navigation)

```
slot_size = align4(key_strlen + 1)   // key
          + 4                          // type tag
          + value_payload_size
```

---

### 5. Nested Structures

#### Array Body

| Offset | Size | Field |
|--------|------|-------|
| +0 | 4 | inner_count — number of elements |
| +4 | varies | Repeated typed values (tag + payload, same encoding as §4) |

Array elements are **tagged values** without keys. Each element is just a type tag followed by its payload, using the same type encodings as top-level slots.

#### Dictionary Body

| Offset | Size | Field |
|--------|------|-------|
| +0 | 4 | inner_count — number of key-value pairs |
| +4 | varies | Repeated key-value slots (same encoding as §3-4) |

Nested dictionaries use **identical** key-value slot encoding as the top level.

#### Body Length Scope

`body_len` covers everything from the inner `count` field to the end of the last slot. It does **not** include the 4-byte `body_len` field itself.

---

### 6. Message Type IDs (msgh_id)

| ID | Direction | Meaning |
|----|-----------|---------|
| `0x10000000` | SEND | Simpleroutine request — fire-and-forget, no reply port in msgh_local_port |
| `0x40000000` \| routine | SEND | Routine request — reply port carried in msgh_local_port (MAKE_SEND, `0x15`) |
| `0x20000000` | SEND (reply) | Routine reply — sent on the send-once right of the reply port |

**Routine ids carry the routine number in the low 16 bits.** The launchd-domain
path (e.g. real `launchctl list`) sends `0x40000000 | routine & 0xffff` —
observed **`0x400000cf`** for `list` on macOS 26.5. Note that Apple's classic
`xpc_pipe_routine` (private API) sends the bare **`0x40000000`** with no low
bits (confirmed by byte capture of the system libxpc, see §11); the routine
bits are added higher in the launchctl call stack. The server demuxes on the
dictionary's `"subsystem"`/`"routine"` keys, not on the msgh_id low bits, so
both encodings interoperate.

---

### 7. Dispatch Path

XPC dispatches on the `flags` field (0x20 in the XPC envelope at offset 0x20):

| flags | msgh_id | Dispatch behavior |
|-------|---------|-------------------|
| `0xf000` | `0x10000000` | Simpleroutine — no reply expected |
| `0xf000` | `0x40000000` | Routine — blocks for reply on send-once right |

---

### 8. Send/Receive Options

The `mach_msg` option parameter passed to `__xpc_send_serializer`:

| Flag | Option Value | Meaning |
|------|-------------|---------|
| flag & 2 == 0 | `0x1` (MACH_SEND_MSG) | Send only |
| flag & 2 == 2 | `0x10001` (MACH_SEND_MSG \| MACH_RCV_MSG) | Combined send+receive |

For combined send+receive, the msgh_size in the mach header encodes the total inline buffer size including receive space.

---

### 9. Complete Layout Diagram (396-byte simpleroutine)

```
Offset  Hex    Field
------  -----  ----------------------------
0x00    13..   msgh_bits = 0x130013
0x04    8c 01  msgh_size = 0x18c = 396
0x08    03 1e  msgh_remote_port
0x0c    00 00  msgh_local_port = 0
0x10    03 1c  msgh_voucher_port
0x14    00 10  msgh_id = 0x10000000
0x18    43 50  magic = "CPX@"
0x1c    05 00  version = 5
0x20    00 f0  flags = 0xf000
0x24    64 01  body_len = 0x164 = 356
0x28    0c 00  count = 12
0x2c    ...    slot 0: null (key="null", type=0x1000)
0x38    ...    slot 1: double (key="double", type=0x5000, 8B)
0x4c    ...    slot 2: string (key="string", type=0x9000, 4B+12B)
0x68    ...    slot 3: data (key="data", type=0x8000, 4B+16B)
0x88    ...    slot 4: array (key="array", type=0xe000, 4B+56B)
0xd0    ...    slot 5: bool (key="bool_true", type=0x2000, 4B)
0xe4    ...    slot 6: uint64 (key="uint64", type=0x4000, 8B)
0xf8    ...    slot 7: dict (key="dict", type=0xf000, 4B+44B)
0x134   ...    slot 8: int64 (key="int64", type=0x3000, 8B)
0x148   ...    slot 9: date (key="date", type=0x7000, 8B)
0x15c   ...    slot 10: uuid (key="uuid", type=0xa000, 16B)
0x178   ...    slot 11: bool (key="bool_false", type=0x2000, 4B)
0x18c         end = 0x2c + 0x164 = 396 ✓
```

---

### 10. Reply Construction

A routine reply is built by:

1. Allocate a mach message header
2. `msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0)` = `0x00000012`
3. `msgh_remote_port` = the send-once right from the original request's `msgh_local_port`
4. `msgh_id = 0x20000000`
5. `msgh_local_port = 0`, `msgh_voucher_port = 0`
6. Inline payload = XPC envelope (`CPX@`, version=5, flags=0xf000) + dictionary body

The reply does **not** go through xpc_pipe — it is sent as a raw `mach_msg` on the send-once right.

#### 10.1 Example 64-Byte Reply (`{"reply": 1}`)

```
Offset  Hex        Field
------  ---------  ----------------------------
0x00    12 00 00   msgh_bits = 0x12 (MOVE_SEND_ONCE)
0x04    40 00 00   msgh_size = 64
0x08    03 1a      msgh_remote_port (send-once from request)
0x0c    00 00      msgh_local_port = 0
0x10    00 00      msgh_voucher_port = 0
0x14    00 00 00   msgh_id = 0x20000000
0x18    43 50 58   magic = "CPX@"
0x1c    05 00 00   version = 5
0x20    00 f0      flags = 0xf000
0x24    18 00 00   body_len = 24
0x28    01 00 00   count = 1
0x2c    72 65 70   key = "reply\0\0\0" (8B, padded)
0x34    00 30      type = 0x3000 (int64)
0x38    01 00      value = 1 (8B)
0x40              end
```

Note: `msgh_size` must equal `64` exactly — earlier builds used `60`, which
truncated the 8-byte int64 to 4 bytes and produced a corrupt (though
tolerated-by-libxpc) message. The size must cover the entire inline payload.

---

### 11. Two Routine Encodings (2026-09 captures)

Two byte layouts are observed in the wild for routine requests:

#### 11.1 Classic pipe contract (this implementation)

Byte-exact capture of the **system libxpc** `xpc_pipe_routine` (probe4b,
local-port loop, interposed `mach_msg`):

```
msgh_bits   = 0x00131513   remote=COPY_SEND(0x13) local=MAKE_SEND(0x15)
                           voucher=COPY_SEND(0x13) — NOT complex
msgh_id     = 0x40000000   (bare — no routine bits on the classic path)
envelope    = offset 24    CPX@ ver=5 flags=0xf000 body_len count...
```

Simple message, envelope immediately after the 24-byte header, reply port in
`msgh_local_port`. The reply is returned on the resulting send-once right
(`MACH_MSG_TYPE_MOVE_SEND_ONCE`). Probe4b's full round-trip (send + hand-built
`{"reply":1}` + parse by system libxpc) validates both directions byte-exactly.

#### 11.2 Modern launchd-domain contract (real launchctl)

Byte-exact capture of re-signed `/bin/launchctl list` against **live launchd**
(arm64e interposer, read-only commands only):

```
msgh_bits   = 0x80131513               complex | remote=COPY_SEND
                                        local=MAKE_SEND voucher=COPY_SEND
msgh_id     = 0x400000cf                base | routine (per-command, below)
offset 24   = 01 00 00 00               msgh_descriptor_count = 1
offset 28   = port descriptor           name = SAME port as msgh_remote_port
                                        (the bootstrap/domain port), pad = 0,
                                        disposition = 0x13 COPY_SEND,
                                        type = 0x00 (MACH_MSG_PORT_DESCRIPTOR)
envelope    = offset 40                 CPX@ ver=5 flags=0xf000 body_len...
```

The reply port is STILL carried in `msgh_local_port` (MAKE_SEND, same as the
classic form) — the descriptor is a redundant send right for the domain port.
The envelope/dict encoding is otherwise identical to the classic form.

**Observed routine ids (read-only commands):**

| Command | msgh_id | Request dict (selected keys) |
|---------|---------|------------------------------|
| `launchctl list` (legacy) | `0x4000032f` | handle, type, legacy, domain-port (mach send `0xd000`) |
| `launchctl version` | `0x4000033c` | handle, shmem (`0xc000`), type, version (bool) |
| `launchctl print` (domain) | `0x4000033c` | shmem (`0xc000`) only — inline wire id, no subsystem/routine keys |
| `launchctl dumpstate` | `0x40000342` | shmem (`0xc000`) only — inline wire id (crash-state dump via shmem) |
| `launchctl status` | `0x400000cf` | handle, instance (uuid), flags, name, type, targetpid, domain-port — the same shape real launchctl emits before every command |
| `launchctl attach` | `0x400002bf` | service subsystem (`2`), routine `703`, plus the parsed service target (name, type, handle) |
| housekeeping probe | `0x400000cf` | handle, instance (uuid), flags, name, type, targetpid, domain-port |
| reply | `0x20000000` | rec_execcnt, req_pid, port (mach send), plus complex OOL descriptor form for the list payload |

**This validates the reverse-engineered constant table**: our
`XPC_ROUTINE_LIST = 0x32f` equals the real legacy-list routine, and the real
`version` uses `0x33c` — the same id as our shmem-state routine family
(`XPC_ROUTINE_PRINT`), consistent with the real request's `shmem` key.

Serialization details confirmed against real captures:

- **mach-send values** (`0xd000`) are encoded **tag-only**: the envelope tag
  keeps the `0xd000` mask and the low byte carries the descriptor-table slot
  (e.g. `00 d0 00 00` = slot 0; `03 d0 00 00` would be slot 3).  probe11's
  ports message (`boot` → bootstrap_port) reproduces this byte-for-byte:
  `00 d0 00 00`, zero payload, right carried by a port descriptor in the
  msgh body.  The real wire's `domain-port` slot is byte-identical to ours
  once the per-process port name differs.
- **shmem values** (`0xc000`) are **tag + 8-byte size**: the tag is followed
  by the memory entry's page-aligned size as a LE uint64.  probe11's ports
  message (`shm` → `xpc_shmem_create(region, 0x4000)`) captures
  `00 c0 00 00 00 40 00 00 00 00 00 00` — tag then 0x4000, exactly the
  region length requested.  Single-page probes against the system show the
  constant ARM64 16K page (`0x4000`) in the same field.  The serializer
  emits the size returned
  by the audited `mach_make_memory_entry_64` in/out parameter; the
  deserializer requires the field on receive.
- **bools** occupy 4 bytes (`01 00 00 00`), matching the value-kind family
  — not a 1-byte slot.

A live round-trip of an *encoded* (serialize → mach-send slot → deserialize)
complex routine request with `domain-port` now reproduces `probe7` variants
A–G byte-for-byte against real launchd; the only deltas are the scheduler's
per-process port names, the voucher port (`0xc03` on real launchd requests),
and dict key order (irrelevant — CPX@ dict pairs are unordered).

Real launchd demuxes on the msgh_id low bits plus per-routine dict keys
(no `subsystem` key on modern requests), whereas this implementation's
stub dispatches on the dict `"subsystem"`/`"routine"` keys — both dialects
are internally consistent, and this tree's wire is the classic
`xpc_pipe_routine` form the system library itself emits.

**Library fidelity** — this tree's own serializer/deserializer now emits
and consumes the confirmed layouts above byte-for-byte:

- **Wire-kind table**: `xpc_internal.h` carries the full kind set
  (`0xb000` fd … `0x1a000` file-transfer) with the tag-anatomy rule: kind
  in bits 8–19, descriptor-slot index in the low byte.  Decoders mask with
  `0xfff00` — a `0xff00` mask aliases `0x12000` (endpoint) onto `0x2000`
  (bool) and misroutes port-backed kinds.
- **Serializer**: `xpc_serialize.c` dissolves an endpoint value into
  `tag 0x12000 | slot` plus a `MACH_MSG_PORT_DESCRIPTOR` in the msgh body —
  the same shape probe11 captured (`0x12000` zero-payload, slot byte;
  shmem as `0xc000` tag + LE u64 size; mach-send as bare `0xd000|slot`).
  The old fallback — `0x6000` uint64 + bare port number — is gone;
  connection values (`0x11000`, layout unconfirmed) degrade to `0x3000`
  null rather than fabricate a layout.
- **Deserializer**: `xpc_deserialize.c` resolves port-backed tags through
  the msgh descriptor table (slot → port name), and the envelope locator
  now walks complex messages — header(24) + descriptor-count(4) + the
  descriptor table (12-byte port descs, 16-byte OOL) — before looking for
  `CPX@`, fixing round-trips of messages that carry send rights.
- **Evidence**: `tests/test_core.c` round-trips an
  `endpoint + mach_send` dict through serialize → complex mach message →
  deserialize-with-ports; the port table holds both rights (slot 0 =
  endpoint's right, slot 1 = mach-send's right) and both decode to the
  expected types with the original port names.

#### 11.3 Live round-trips against real launchd (probe_routine)

Driving the system libxpc (`_xpc_domain_routine` via dlsym) against the
live bootstrap port closed both reply-side questions empirically:

**Version (routine `0x33c`)** — request must carry a shared-memory region:
`{handle:0, shmem: <xpc_shmem_create(region, 0x1000)>, type:1, version:true}`.
launchd writes the version string into the caller's region and replies
`{"bytes-written": 128}`:

```
Darwin Bootstrapper Version 7.0.0: Sat Apr 18 19:58:40 PDT 2026;
root:libxpc_executables-3102.120.13~112/launchd/RELEASE_ARM64E
```

This is now reproduced end to end by this tree's own serializer and pipe
(`tools/probe/probe9.c`): the request's shmem slot is the 12-byte
tag + page-size value, the reply payload is byte-identical to the real
library's capture from `{"bytes-written"` onward, and launchd maps our
memory entry and fills the region. The only remaining deltas are the
per-process reply-port name and the voucher port (ours sends none;
launchd accepts the voucher-less form).

**Legacy list (routine `0x32f`)** — the minimal request `{type:1, handle:0}`
suffices; extra keys are ignored. The reply is a plain dictionary (no OOL,
no shmem), 437 jobs on this system, wrapped under a `"services"` key with
per-job entries `{pid: <int64>, status: <int64>}`. This is semantically the
same contract this implementation's stub emits — the stub's per-job dicts
are exactly `pid`/`status` as int64 — with two cosmetic deltas: the real
reply nests the table under `"services"`, and real per-job dicts carry
exactly those two keys (ours adds `active count`/`path`/`program`).

The simple classic wire form is sufficient for both commands (Apple's own
client emits it); the complex/descriptor preamble observed in launchctl
traffic is not required by launchd for these routines.

---

### Appendix A. Observed Correlation with libxpc Internals

| Function | Address | Role |
|----------|---------|------|
| `__xpc_pipe_pack_message` | 0x1801d4b58 | Serializes xpc_object_t into mach message buffer |
| `__xpc_send_serializer` | 0x1801d50b0 | Wraps `mach_msg` call with flag-based option selection |
| `__xpc_pipe_mach_msg` | 0x1801f34b4 | Combined send+receive: `mach_msg(msg, option, msg->msgh_size, rcv_size, rcv_name, 0, 0)` |
| `__xpc_pipe_routine` | 0x1801f2ca8 | Full routine lifecycle: serialize → send → wait for reply (checks msgh_id == 0x20000000) |
| `_xpc_pipe_routine_reply` | 0x1801f2e40 | Builds and sends reply via `__xpc_pipe_pack_message(0,0,dict,0,0,0)` → `__xpc_send_serializer` |
| `__xpc_send_serializer` call site | 0x1801d5118 | Sets option: `(flag & 2) ? 0x10001 : 0x1` |

---

*This specification was generated from byte-exact captures of real XPC traffic, validated against libxpc disassembly, and confirmed by feeding hand-built messages back into libxpc's parser.*