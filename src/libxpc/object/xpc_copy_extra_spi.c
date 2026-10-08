#include "xpc_internal.h"

char *
xpc_copy_clean_description(xpc_object_t object)
{
    (void)object;
    return NULL;
}

xpc_object_t
xpc_copy_code_signing_identity_for_token(audit_token_t token)
{
    (void)token;
    return NULL;
}

char *
xpc_copy_debug_description(xpc_object_t object)
{
    (void)object;
    return NULL;
}

const void *
xpc_copy_entitlement_for_self(const char *entitlement)
{
    (void)entitlement;
    return NULL;
}

void *
xpc_copy_entitlements_data_for_token(audit_token_t token)
{
    (void)token;
    return NULL;
}

const void *
xpc_copy_entitlements_for_self(void)
{
    return NULL;
}

const void *
xpc_copy_event_entitlements(xpc_object_t object)
{
    (void)object;
    return NULL;
}
