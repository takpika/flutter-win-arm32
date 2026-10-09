/* wpnotify: notifications for Windows 10 Mobile UWP apps without WNS.
 *
 * A plain C library on the WinRT ABI. The app keeps one TCP connection to its
 * own server and hands it to the OS socket broker (SocketActivityTrigger);
 * a native background task built on this library handles traffic while the
 * app is not running and can show toasts or the system incoming-call UI.
 *
 * Reconnection settings are read from LocalState\wpnotify.cfg, written by the
 * app when it registers its tasks:
 *   host=<server host>
 *   port=<server port>
 *   task=<GUID of the SocketActivityTrigger task registration>
 *   socket=<socket id passed to TransferOwnership>
 */
#ifndef WPNOTIFY_H
#define WPNOTIFY_H

#include <stddef.h>
#include <windows.h>
#include <inspectable.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wpn_socket wpn_socket;

typedef struct wpn_socket_handlers {
    /* Data arrived on the brokered socket. Ownership is returned to the broker
     * after the callback; wpn_socket_send may be used inside it. */
    void (*on_data)(wpn_socket *socket, const char *data, size_t length, void *context);
    /* The broker's keep-alive timer expired; send something to the server. */
    void (*on_keepalive)(wpn_socket *socket, void *context);
    /* A fresh connection replaced a closed one (socket closed or system
     * trigger such as session connected after a reboot). */
    void (*on_reconnected)(wpn_socket *socket, const char *reason, void *context);
    void *context;
    /* Binary protocols may consume a complete frame directly on each wake. */
    HRESULT (*on_activity)(wpn_socket *, void *context);
    HRESULT (*on_connected)(wpn_socket *, void *context);
    const wchar_t *config_file; /* NULL: wpnotify.cfg */
    unsigned keepalive_seconds; /* 0 keeps the OS default */
} wpn_socket_handlers;

/* Entry point for IBackgroundTask::Run. `task_instance` is the
 * IBackgroundTaskInstance pointer given to Run. Handles socket activity,
 * keep-alive, closed sockets and non-socket triggers (reconnect). */
HRESULT wpn_socket_task_run(void *task_instance, const wpn_socket_handlers *handlers);

HRESULT wpn_socket_send(wpn_socket *socket, const void *data, size_t length);
HRESULT wpn_socket_receive(wpn_socket *socket, void *data, size_t capacity, size_t *length);

typedef struct wpn_socket_setup_info {
    const char *host, *port;   /* server */
    const char *entry_point;   /* class of the app's socket task (manifest) */
    const char *task_name;     /* registration name prefix; default "wpnotify-socket" */
    const char *socket_id;     /* default "wpnotify" */
    const char *hub_id;        /* NULL keeps the stored one */
    int tls;                  /* OS-validated TLS 1.2, persisted for reconnect */
} wpn_socket_setup_info;

/* One-time setup from the foreground app: registers the socket task plus the
 * session-connected (reboot) and servicing-complete (update) tasks, writes
 * LocalState\wpnotify.cfg, connects and hands the socket to the broker.
 * `handlers->on_reconnected` is called with reason "setup" to say hello.
 * Call wpn_request_background_access first. */
HRESULT wpn_socket_setup(const wpn_socket_setup_info *info, const wpn_socket_handlers *handlers);

/* Sends one line to the configured server on a short-lived connection and
 * returns the first reply line (used for channel requests). */
HRESULT wpn_server_request(const char *line, char *reply, size_t reply_size);

/* Shows a two-line toast for the calling package. UTF-8 strings. */
HRESULT wpn_toast(const char *title, const char *body);
/* Same calling-package Windows toast, with opaque foreground activation args.
 * UTF-8: title <=4096, body <=8192, launch <=4096 bytes; no truncation. NULL
 * means empty text/args. Display timing, message storage and tap dispatch are
 * owned by the caller. No Hub registration or Android channel is consulted. */
HRESULT wpn_toast_with_launch(const char *title, const char *body, const char *launch_arguments);
/* Generic Windows notification APIs, always scoped to the calling package.
 * Setting returns the actual Windows NotificationSetting enum (0 = enabled).
 * Post returns S_FALSE when Windows settings disable notifications. Tag/group
 * are limited to 16 UTF-16 code units for the supported Mobile OS versions.
 * No FCM field interpretation or app consent state lives in these helpers. */
HRESULT wpn_toast_get_setting(int *setting);
HRESULT wpn_toast_post_xml(const char *xml, const char *tag, const char *group, int suppress_popup);
/* Match the existing tag/group and opaque launch argument, for replay without
 * posting the same visible notification twice. */
HRESULT wpn_toast_matches(const char *tag, const char *group, const char *launch, int *matches);

typedef enum wpn_call_event {
    WPN_CALL_RINGING = 0,
    WPN_CALL_ANSWERED,         /* detail: accepted media (VoipPhoneCallMedia flags) */
    WPN_CALL_REJECTED,         /* detail: VoipPhoneCallRejectReason (0 ignored, 1 timed out, ...) */
    WPN_CALL_END_REQUESTED,    /* user hung up from the system UI */
    WPN_CALL_HOLD_REQUESTED,
    WPN_CALL_RESUME_REQUESTED,
    WPN_CALL_NO_RESPONSE,      /* no event before the ring timeout (library ended the call) */
    WPN_CALL_ACTIVE_TIMEOUT,   /* answered call reached max_active_seconds (library ended it) */
    WPN_CALL_CANCELED,         /* wpn_cancel_incoming_call (e.g. the caller hung up) */
    WPN_CALL_ENDED             /* NotifyCallEnded was sent; always the last event */
} wpn_call_event;

typedef void (*wpn_call_callback)(wpn_call_event event, int detail, void *context);

typedef struct wpn_call_request {
    const char *context;      /* passed to the app when the call is opened; required */
    const char *contact_name;
    const char *contact_number;
    const char *service_name;
    const char *call_details;
    unsigned ring_timeout_seconds;  /* 5..120, default 30 */
    unsigned max_active_seconds;    /* how long an answered call is kept; default 60 */
    wpn_call_callback callback;     /* optional; called from the task or event threads */
    void *callback_context;
} wpn_call_request;

/* Shows the system incoming-call UI and blocks until the call is over:
 * rejected, ended by the user, not answered within the ring timeout, or kept
 * for max_active_seconds after answering. Hold/resume requests are
 * acknowledged automatically. `outcome` (optional) receives the event that
 * finished the call. Requires the uap:voipCall capability and the
 * windows.voipCall application extension. */
HRESULT wpn_incoming_call(const wpn_call_request *request, wpn_call_event *outcome);

/* Ends the call currently handled by wpn_incoming_call, from any process of
 * the same package (for example the socket task when the server reports that
 * the caller hung up). Uses a named event in the package's object namespace;
 * a cancel with no call in progress is ignored. */
HRESULT wpn_cancel_incoming_call(void);

/* Starts the app's VoIP call task (a background task declared in the
 * manifest, activated by class name) through
 * VoipCallCoordinator.ReserveCallResourcesAsync. The call task runs without
 * the normal background time limit while a call is in progress, so it is the
 * place to call wpn_incoming_call. `status` receives
 * VoipPhoneCallResourceReservationStatus (0 success, 1 resources not
 * available). */
HRESULT wpn_reserve_call_resources(const char *task_entry_point, int *status);

/* Two-step calls, so ringing does not depend on the caller's time limit:
 * wpn_start_call stores the caller and reserves call resources, which starts
 * the app's call task; the call task then calls wpn_call_task_run. Works from
 * any background task (socket task, delivery app service). */
HRESULT wpn_start_call(const char *call_task_entry_point, const char *contact_name);
/* `request` supplies everything except contact_name, which comes from
 * wpn_start_call. Returns S_FALSE when no call is pending. */
HRESULT wpn_call_task_run(void *task_instance, const wpn_call_request *request, wpn_call_event *outcome);

/* Asks for background execution (call once from the foreground app; the
 * result is BackgroundAccessStatus). Also needed again after app updates. */
HRESULT wpn_request_background_access(int *status);

/* Hub delivery through App Services.
 *
 * One hub app keeps the server connection; each receiving app exposes the
 * app service WPN_DELIVERY_SERVICE (manifest: windows.appService) and shows
 * its own toasts or calls, so they carry the receiving app's name.
 * Messages are ValueSets with a string "payload" (request) and "status"
 * (response). */
#define WPN_DELIVERY_SERVICE "wpnotify.deliver"

/* Sends `payload` to any app service and returns the response's "status". */
HRESULT wpn_app_service_call(const char *package_family_name, const char *service_name, const char *payload,
                             char *reply, size_t reply_size);

/* Hub side: opens the receiving app's delivery service and sends `payload`.
 * `reply` (optional) receives the "status" string of the response. */
HRESULT wpn_deliver(const char *package_family_name, const char *payload, char *reply, size_t reply_size);

/* Receiving side: handles one delivery request. Return the status text in
 * `reply` (for example "ok"). Runs inside the app service background task. */
typedef void (*wpn_delivery_handler)(const char *caller_package_family_name, const char *payload,
                                     char *reply, size_t reply_size, void *context);

/* Entry point for IBackgroundTask::Run of the delivery app service task.
 * Waits for the request, calls `handler`, sends the response, then returns. */
HRESULT wpn_delivery_task_run(void *task_instance, wpn_delivery_handler handler, void *context);

/* ---- WNS drop-in ------------------------------------------------------
 * Replace
 *   PushNotificationChannelManager.CreatePushNotificationChannelForApplicationAsync()
 * with wpn_create_channel_for_application() and send channel->uri to your
 * server as before. The server posts to it exactly as to WNS (X-WNS-Type
 * wns/toast|tile|badge|raw). Then add to the manifest (see README):
 *   - the "wpnotify.deliver" app service -> Wpnotify.DeliverTask (wpnotify.dll)
 * Toasts, tiles and badges are shown from the XML; raw notifications start
 * the app's existing pushNotification background task with a RawNotification.
 * Requires the WpnHub app on the device. */
#define WPN_HUB_PACKAGE_FAMILY_NAME "MacMobile.WpnHub_nyxx8p1k80n0p"
#define WPN_HUB_SERVICE "wpnotify.hub"

typedef struct wpn_channel {
    char uri[512];
    long long expiration_unix_seconds;
} wpn_channel;

HRESULT wpn_create_channel_for_application(wpn_channel *channel);

/* Library task classes (wpnotify.dll registers these names):
 *   Wpnotify.DeliverTask, Wpnotify.HubSocketTask, Wpnotify.HubChannelService */
HRESULT wpn_wns_delivery_task_run(IInspectable *task_instance);
HRESULT wpn_hub_socket_task_run(IInspectable *task_instance);
HRESULT wpn_hub_channel_service_run(IInspectable *task_instance);
extern const wpn_socket_handlers wpn_hub_handlers;

/* Background task classes of the app DLL. The app defines this table; the
 * library implements IBackgroundTask, the activation factories and the DLL
 * exports (DllGetActivationFactory, DllCanUnloadNow). Each class must also be
 * listed under windows.activatableClass.inProcessServer in the manifest. */
typedef struct wpn_task_class {
    const wchar_t *class_name;
    HRESULT (*run)(IInspectable *task_instance);
} wpn_task_class;
extern const wpn_task_class wpn_task_classes[];
extern const size_t wpn_task_class_count;

/* Reads one value of LocalState\wpnotify.cfg ("host", "port", "hub", "socket"). */
HRESULT wpn_config_get(const char *key, char *out, size_t size);

/* Appends a timestamped line to LocalState\wpnotify.log. */
void wpn_log(const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
