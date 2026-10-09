#ifndef WPN_BACKGROUND_H
#define WPN_BACKGROUND_H
#include "wpnotify.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct wpn_background_scope wpn_background_scope;
/* Acquire synchronously inside the activation/Run callback, before returning
 * or handing work to another thread. Owns a deferral and cancellation handler.
 * Exactly one end is required on every successful begin, after worker/context
 * cleanup. Scope can move to a WinRT-initialized worker; no thread affinity. */
HRESULT wpn_background_begin(IInspectable *instance, wpn_background_scope **scope);
int wpn_background_canceled(const wpn_background_scope *scope);
/* Resolve an owning task-instance reference on the current apartment. */
HRESULT wpn_background_task(const wpn_background_scope *scope, IInspectable **instance);
HRESULT wpn_background_end(wpn_background_scope *scope);
#ifdef __cplusplus
}
#endif
#endif
