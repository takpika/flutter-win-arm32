#ifndef WPN_FCM_H
#define WPN_FCM_H
#include "wpnotify.h"
#include "wpn_fcm_message.h"
#include "wpn_fcm_response.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *android_package;
    const char *sender_id;
    const char *certificate_sha1; /* Original Android signing certificate, 40 hex chars */
    const char *firebase_app_id;  /* Optional for legacy GCM registration */
    const char *installation_id;
    const char *installation_auth; /* Firebase Installations auth, never logged */
} wpn_fcm_options;
/* Blocking; call on a worker thread. Always contacts Google, allowing renewal.
 * Caller identity is supplied by App Service, never by options. Token is secret. */
HRESULT wpn_fcm_get_token(const wpn_fcm_options *options, char *token, size_t capacity);
/* Delete this package/sender registration at Google, then remove its Hub route.
 * Idempotent for an absent route; never removes another sender or app. */
HRESULT wpn_fcm_delete_token(const wpn_fcm_options *options);
/* Bare Android topic name, 1..900 ASCII [a-zA-Z0-9-_.~%]. Token comes from
 * get_token for the same options. These calls do not create a registration. */
HRESULT wpn_fcm_subscribe(const wpn_fcm_options *options, const char *token, const char *topic);
HRESULT wpn_fcm_unsubscribe(const wpn_fcm_options *options, const char *token, const char *topic);
/* Additive raw-response forms; initialize response.size (V1), or v2.base.size
 * to sizeof(wpn_fcm_response_v2) and pass &v2.base for a retry deadline.
 * The response carries
 * only HTTP status and a bounded backend label. Transport failures retain their
 * HRESULT; a remote failure returns ERROR_BAD_NET_RESP with raw metadata.
 * Existing forms keep their conservative failure behavior. */
HRESULT wpn_fcm_get_token_ex(const wpn_fcm_options*,char *token,size_t capacity,wpn_fcm_response*);
HRESULT wpn_fcm_delete_token_ex(const wpn_fcm_options*,wpn_fcm_response*);
HRESULT wpn_fcm_subscribe_ex(const wpn_fcm_options*,const char *token,const char *topic,wpn_fcm_response*);
HRESULT wpn_fcm_unsubscribe_ex(const wpn_fcm_options*,const char *token,const char *topic,wpn_fcm_response*);
/* Activate/initialize the hidden Hub through App Service. Does not open UI.
 * An OS background-access denial is returned as E_ACCESSDENIED. Call on a
 * worker during SDK initialization; it does not replace FIS/token management. */
HRESULT wpn_fcm_initialize(void);
/* Handler returns success only after the application has durably accepted data.
 * Delivery may repeat after a crash; deduplicate by persistent_id/message id. */
typedef HRESULT (*wpn_fcm_handler)(const wpn_fcm_message *, void *context);
HRESULT wpn_fcm_delivery_task_run(void *instance, wpn_fcm_handler handler, void *context);
/* WpnHub foreground setup; separate brokered socket/config from the WNS path. */
HRESULT wpn_fcm_hub_setup(void);
HRESULT wpn_fcm_hub_initialize(void);
HRESULT wpn_fcm_hub_socket_task_run(IInspectable *instance);
/* Internal dispatch used by the existing HubChannelService. Returns 0 if WNS. */
int wpn_fcm_hub_request(const char *caller, const char *payload, char *reply, size_t size);
#ifdef __cplusplus
}
#endif
#endif
