#include "xpc_internal.h"

void libxpc_initializer(void);
void _libxpc_initializer(void);

void _libxpc_initializer(void)
{
    libxpc_initializer();
}
