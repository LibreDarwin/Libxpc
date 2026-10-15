#include "xpc_internal.h"

void libxpc_initializer(void);
void __libxpc_initializer(void);

void __libxpc_initializer(void)
{
    libxpc_initializer();
}
