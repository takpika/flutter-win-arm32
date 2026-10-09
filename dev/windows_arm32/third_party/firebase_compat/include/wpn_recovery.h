/* Generic Windows task registration; contains no Firebase receipt policy. */
#ifndef WPN_RECOVERY_H
#define WPN_RECOVERY_H
#include "wpnotify.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Idempotently ensure timer (15 minutes), session-connected and servicing tasks.
 * Prefix: 1..40 ASCII letters/digits/._-. Use a new prefix for a changed task
 * entry point. Empty entry_point leaves TaskEntryPoint unset for in-process
 * BackgroundActivated on Windows 10 1607+. No network condition is imposed.
 * Missing registrations are repaired by name, including after a process dies
 * before writing app metadata. Other app tasks are not modified.
 * S_OK means all three registered; OS policy still controls execution.
 * E_ACCESSDENIED with access_status set means foreground-only recovery.
 * The receiving SDK must persist its own configuration before acceptance. */
HRESULT wpn_recovery_register(const char *entry_point, const char *prefix, int *access_status);
/* Match only the exact three task names owned by prefix. */
HRESULT wpn_recovery_is_task(IInspectable *instance, const char *prefix, int *matches);
#ifdef __cplusplus
}
#endif
#endif
