#include <mach/mach.h>

kern_return_t
launch_active_user_login(audit_token_t token, int32_t user_login_uid, int32_t user_uid)
{
    (void)token; (void)user_login_uid; (void)user_uid;
    return KERN_FAILURE;
}

kern_return_t
launch_active_user_logout(audit_token_t token)
{
    (void)token;
    return KERN_FAILURE;
}
