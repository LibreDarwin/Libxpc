#import <Foundation/Foundation.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The internal xpc header is written for the pure-C translation units and
 * collides with the SDK's xpc/xpc.h that Foundation drags in, so this ObjC
 * file relies on the SDK's own xpc declarations (pulled in transitively) and
 * only forward-declares the one port-internal entry point it calls.
 */
extern void xpc_release(xpc_object_t object);

static xpc_object_t xpc_convert_plist_object(id obj);


static xpc_object_t
xpc_convert_plist_array(NSArray *array)
{
    xpc_object_t result = xpc_array_create(NULL, 0);
    for (id element in array) {
        xpc_object_t converted = xpc_convert_plist_object(element);
        if (converted != NULL) {
            xpc_array_append_value(result, converted);
            xpc_release(converted);
        }
    }
    return result;
}

static xpc_object_t
xpc_convert_plist_dictionary(NSDictionary *dict)
{
    xpc_object_t result = xpc_dictionary_create(NULL, NULL, 0);
    for (id key in dict) {
        if (![key isKindOfClass:[NSString class]]) {
            continue;
        }
        const char *ckey = [key UTF8String];
        if (ckey == NULL) {
            continue;
        }
        xpc_object_t converted = xpc_convert_plist_object(dict[key]);
        if (converted != NULL) {
            xpc_dictionary_set_value(result, ckey, converted);
            xpc_release(converted);
        }
    }
    return result;
}

static xpc_object_t
xpc_convert_plist_number(NSNumber *number)
{
    const char *type = [number objCType];
    if (type != NULL && type[0] == 'c') {
        /* CFBoolean is an NSNumber wrapping a char 'c'; plists never carry
         * raw 8-bit ints, so a 'c' always means a boolean. */
        return xpc_bool_create([number boolValue]);
    }
    if (type != NULL && (type[0] == 'f' || type[0] == 'd')) {
        return xpc_double_create([number doubleValue]);
    }
    if ([number longLongValue] < 0) {
        return xpc_int64_create([number longLongValue]);
    }
    return xpc_uint64_create([number unsignedLongLongValue]);
}

static xpc_object_t
xpc_convert_plist_object(id obj)
{
    if (obj == nil || [obj isKindOfClass:[NSNull class]]) {
        return xpc_null_create();
    }
    if ([obj isKindOfClass:[NSDictionary class]]) {
        return xpc_convert_plist_dictionary(obj);
    }
    if ([obj isKindOfClass:[NSArray class]]) {
        return xpc_convert_plist_array(obj);
    }
    if ([obj isKindOfClass:[NSString class]]) {
        return xpc_string_create([obj UTF8String]);
    }
    if ([obj isKindOfClass:[NSData class]]) {
        return xpc_data_create([obj bytes], [obj length]);
    }
    if ([obj isKindOfClass:[NSNumber class]]) {
        return xpc_convert_plist_number(obj);
    }
    if ([obj isKindOfClass:[NSDate class]]) {
        /* xpc dates carry nanoseconds since the unix epoch. */
        double unix_seconds = [obj timeIntervalSince1970];
        return xpc_date_create((int64_t)(unix_seconds * 1000000000.0));
    }
    return xpc_null_create();
}

/*
 * Read <dir>/Contents/Info.plist and return it as an xpc dictionary, or NULL
 * if the file is absent or unparseable.  `dir` is the bundle container.
 */
xpc_object_t
xpc_create_dictionary_from_bundle_path(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return NULL;
    }

    @autoreleasepool {
        char plist_path[2048];
        int written = snprintf(plist_path, sizeof(plist_path),
            "%s/Contents/Info.plist", dir);
        if (written <= 0 || (size_t)written >= sizeof(plist_path)) {
            return NULL;
        }

        NSString *path = [NSString stringWithUTF8String:plist_path];
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (data == nil) {
            return NULL;
        }

        NSError *error = nil;
        id plist = [NSPropertyListSerialization
            propertyListWithData:data
                         options:NSPropertyListImmutable
                          format:NULL
                           error:&error];
        if (plist == nil) {
            return NULL;
        }
        if (![plist isKindOfClass:[NSDictionary class]]) {
            return NULL;
        }

        return xpc_convert_plist_dictionary(plist);
    }
}
