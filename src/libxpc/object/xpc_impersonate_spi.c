#include <sys/types.h>
#include "xpc_internal.h"

int
xpc_impersonate_user(uid_t uid)
{
    (void)uid;
    return -1;
}
