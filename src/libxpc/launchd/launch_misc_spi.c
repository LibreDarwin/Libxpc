#include <mach/mach.h>
#include <stdint.h>

kern_return_t
launch_activate_socket(const char *name, int *socks, size_t sockc)
{
    (void)name;
    (void)socks;
    (void)sockc;
    return KERN_FAILURE;
}
