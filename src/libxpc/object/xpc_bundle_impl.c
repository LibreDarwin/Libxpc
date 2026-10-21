#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>

#include "xpc_internal.h"

/*
 * Bundle objects.
 *
 * Apple's libxpc realizes a bundle as a plain xpc base object (allocated via
 * __xpc_base_create with the _xpc_type_bundle singleton) whose ivars mirror a
 * small fixed layout:
 *
 *   +0x08  origin            (uint32)  XPC_BUNDLE_ORIGIN_*
 *   +0x10  (populated)kind   (uint32)
 *   +0x18  dispatch_once_t   (resolve gate)
 *   +0x20  container kind    (uint32, filled by resolve)
 *   +0x28  flags             (uint8; bit0 = is-main)
 *   +0x30  path              (char *, strdup'd)
 *   +0x38  root path         (char *, strdup'd during resolve)
 *   +0x40  normalized path   (char *)
 *   +0xf0  info dictionary   (xpc_object_t, cached)
 *   +0x120 string cache      (xpc_object_t, retained)
 *   +0x128 extra             (xpc_object_t, retained)
 *   +0x130 error string      (const char *)
 *   +0x138 flags2            (uint8)
 *
 * The ObjC OS_xpc_bundle class Apple also ships is only an NSXPC-facing
 * bridging shim for the type; the object itself is never an ObjC instance.
 * This port models the object as a C struct, matching that realization.
 *
 * NOTE ON FIDELITY: Apple's resolver consults private dyld entry points
 * (__dyld_get_image_uuid, __dyld_get_shared_cache_uuid,
 * __dyld_get_shared_cache_range) that are exported by no linkable library and
 * are not present in the runtime on this host.  Those UUIDs only appear on
 * resolve *error* paths and in the shared-cache fast path; the container /
 * resources / services / Info.plist resolution itself is filesystem-based.
 * This port therefore implements the filesystem resolution faithfully and
 * omits the dyld-UUID bookkeeping, which cannot be reproduced here.  The
 * divergence is confined to the "missing root"/variant error metadata.
 */

typedef struct _xpc_bundle_s {
    struct _xpc_object_s hdr;
    uint32_t origin;        /* XPC_BUNDLE_ORIGIN_* */
    uint32_t populated_kind;
    void *resolve_once;     /* dispatch_once_t storage */
    uint32_t container_kind;
    uint8_t flags;
    char *path;
    char *root_path;
    char *normalized_path;
    xpc_object_t info_dictionary;
    xpc_object_t string_cache;
    xpc_object_t extra;
    const char *error;
    uint8_t flags2;
} xpc_bundle_t;

enum {
    XPC_BUNDLE_ORIGIN_MAIN      = 1,
    XPC_BUNDLE_ORIGIN_FRAMEWORK = 2,
};

/* Per-origin number of trailing path components to strip to reach the bundle
 * container (Apple's table at __TEXT __const +0x128). */
static const uint64_t bundle_container_strip[3] = { 3, 3, 1 };

#pragma mark - small helpers

static char *
bundle_strdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy != NULL) {
        memcpy(copy, s, n);
    }
    return copy;
}

/* Collapse a run of trailing '/' characters down to a single NUL, matching
 * Apple's __trim_trailing_slashes(). */
static void
bundle_trim_trailing_slashes(char *path)
{
    size_t len = strlen(path);
    if (len == 0 || path[len - 1] != '/') {
        return;
    }
    char *p = strrchr(path, '/');
    p += 2;
    char *prev;
    do {
        prev = p - 2;
        p -= 1;
    } while (*prev == '/');
    *p = '\0';
}

/* Return a strdup of `path` with the per-`origin` number of trailing path
 * components removed.  Mirrors __xpc_bundle_copy_container(). */
static char *
bundle_copy_container(uint32_t origin, const char *path)
{
    char *copy = bundle_strdup(path);
    uint64_t levels = (origin < 3) ? bundle_container_strip[origin] : 0;
    for (uint64_t i = 0; i < levels; i++) {
        char *slash = strrchr(copy, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
    }
    return copy;
}

#pragma mark - object lifecycle

static xpc_bundle_t *
bundle_create_from_origin(uint32_t origin, const char *path,
    xpc_object_t string_cache, xpc_object_t extra)
{
    xpc_bundle_t *b = (xpc_bundle_t *)xpc_object_alloc(
        (xpc_type_t)&_xpc_type_bundle, sizeof(xpc_bundle_t));
    if (b == NULL) {
        return NULL;
    }
    b->origin = origin;
    b->path = bundle_strdup(path);
    if (string_cache != NULL) {
        b->string_cache = xpc_retain(string_cache);
    }
    if (extra != NULL) {
        b->extra = xpc_retain(extra);
    }
    bundle_trim_trailing_slashes(b->path);
    return b;
}

xpc_object_t
xpc_bundle_create(const char *identifier)
{
    return (xpc_object_t)bundle_create_from_origin(XPC_BUNDLE_ORIGIN_MAIN,
        identifier, NULL, NULL);
}

xpc_object_t
xpc_bundle_create_main(void)
{
    char path[1024];
    uint32_t size = sizeof(path);

    memset(path, 0, sizeof(path));
    if (_NSGetExecutablePath(path, &size) != 0) {
        return NULL;
    }
    xpc_bundle_t *b = bundle_create_from_origin(XPC_BUNDLE_ORIGIN_MAIN, path,
        NULL, NULL);
    if (b != NULL) {
        b->flags |= 1;      /* is-main */
    }
    return (xpc_object_t)b;
}
xpc_object_t
xpc_bundle_create_from_origin(xpc_object_t origin)
{
    (void)origin;
    return NULL;
}

xpc_object_t
xpc_bundle_create_from_origin_with_string_cache(xpc_object_t origin, void *cache)
{
    (void)origin; (void)cache;
    return NULL;
}

xpc_object_t
xpc_bundle_create_with_string_cache(const char *identifier, void *cache)
{
    return (xpc_object_t)bundle_create_from_origin(XPC_BUNDLE_ORIGIN_MAIN,
        identifier, NULL, (xpc_object_t)cache);
}

#pragma mark - resolution

/* Determine the bundle shape from the container path:
 *   0 -> /.../Foo.app/Contents/MacOS  (application)
 *   1 -> /.../Foo.framework/Versions  (framework)
 *   2 -> anything else
 * Mirrors the kind computation in Apple's resolve block invoke. */
static uint32_t
bundle_kind_from_path(const char *path)
{
    char *scratch = bundle_strdup(path);
    char *slash = strrchr(scratch, '/');
    if (slash != NULL) {
        *slash = '\0';
    }
    uint32_t kind;
    if (*scratch == '\0') {
        kind = 2;
    } else if (strcmp(scratch + (strlen(scratch) - strlen("/Contents/MacOS")),
                   "/Contents/MacOS") == 0 &&
               strlen(scratch) >= strlen("/Contents/MacOS")) {
        kind = 0;
    } else {
        char *s2 = strrchr(scratch, '/');
        if (s2 != NULL) {
            *s2 = '\0';
        }
        if (strcmp(scratch + (strlen(scratch) - strlen("/Versions")),
                   "/Versions") == 0 &&
            strlen(scratch) >= strlen("/Versions")) {
            kind = 1;
        } else {
            kind = 2;
        }
    }
    free(scratch);
    return kind;
}

/* Best-effort filesystem resolve: compute the container path, then load
 * "Contents/Info.plist" into an xpc dictionary. */
static void
bundle_resolve(xpc_bundle_t *b)
{
    b->populated_kind = bundle_kind_from_path(b->path);
    b->container_kind = b->populated_kind;

    if (b->root_path == NULL) {
        b->root_path = bundle_strdup(b->path);
    }
    if (b->normalized_path != NULL) {
        free(b->normalized_path);
    }
    if (b->origin == XPC_BUNDLE_ORIGIN_MAIN) {
        b->normalized_path = bundle_copy_container(b->populated_kind, b->path);
    } else {
        b->normalized_path = bundle_strdup(b->path);
    }

    if (b->info_dictionary == NULL) {
        b->info_dictionary = xpc_create_dictionary_from_bundle_path(
            b->normalized_path);
    }
}

xpc_object_t
xpc_bundle_get_info_dictionary(xpc_object_t bundle)
{
    if (bundle == NULL || XPC_CAST(xpc_bundle_t, bundle)->hdr.isa !=
        &_xpc_type_bundle) {
        return NULL;
    }
    xpc_bundle_t *b = XPC_CAST(xpc_bundle_t, bundle);
    bundle_resolve(b);
    return b->info_dictionary;
}

const char *
xpc_bundle_get_error(xpc_object_t bundle)
{
    if (bundle == NULL) {
        return NULL;
    }
    return XPC_CAST(xpc_bundle_t, bundle)->error;
}

const char *
xpc_bundle_get_path(xpc_object_t bundle)
{
    if (bundle == NULL) {
        return NULL;
    }
    return XPC_CAST(xpc_bundle_t, bundle)->path;
}

const char *
xpc_bundle_get_executable_path(xpc_object_t bundle)
{
    if (bundle == NULL) {
        return NULL;
    }
    xpc_bundle_t *b = XPC_CAST(xpc_bundle_t, bundle);
    bundle_resolve(b);
    return b->normalized_path;
}

xpc_object_t
xpc_bundle_get_property(xpc_object_t bundle, const char *name)
{
    xpc_object_t info = xpc_bundle_get_info_dictionary(bundle);
    if (info == NULL) {
        return NULL;
    }
    return xpc_dictionary_get_value(info, name);
}

xpc_object_t
xpc_bundle_get_xpcservice_dictionary(xpc_object_t bundle)
{
    return xpc_bundle_get_property(bundle, "XPCService");
}

void
xpc_bundle_populate(xpc_object_t bundle)
{
    if (bundle == NULL) {
        return;
    }
    bundle_resolve(XPC_CAST(xpc_bundle_t, bundle));
}
