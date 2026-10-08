#include "xpc_internal.h"

int
reboot3(uint32_t howto, const char *message)
{
    (void)howto; (void)message;
    return -1;
}
