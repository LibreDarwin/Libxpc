#include <sys/types.h>
#include <mach/mach.h>

kern_return_t
launch_bootout_user_service_4coresim(audit_token_t token, const char *username, uid_t uid, int32_t flags)
{
    (void)token; (void)username; (void)uid; (void)flags;
    return KERN_FAILURE;
}

kern_return_t
launch_bootout_user_service_4coresim_with_flags(audit_token_t token, const char *username, uid_t uid, int32_t flags, int32_t coredump_flags)
{
    (void)token; (void)username; (void)uid; (void)flags; (void)coredump_flags;
    return KERN_FAILURE;
}
