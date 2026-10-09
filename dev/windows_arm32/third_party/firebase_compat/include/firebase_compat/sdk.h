/* Development C ABI for the shared Firebase compatibility SDK.
 * The Framework executes in the caller's package/process. This development ABI
 * is not an integration release; see MIGRATION.md for remaining lifecycle gates.
 */
#ifndef FIREBASE_COMPAT_SDK_H
#define FIREBASE_COMPAT_SDK_H
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include <wpn_fcm_message.h>
#include "notification_settings.h"
#include "messaging_error.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FCM_SDK_ABI_VERSION UINT32_C(0x00000016)
typedef uint64_t fcm_sdk_handle;
typedef struct fcm_sdk_configuration {
    uint32_t size;
    const char *app_name, *project_id, *app_id;
    /* NULL/empty uses in-process BackgroundActivated. Must be routed by host.
     * Recovery names belong to this SDK; all Firebase app instances in a
     * package use the same entry point and the same package-private store. */
    const char *background_entry_point;
    int background_observer;
} fcm_sdk_configuration;
typedef struct fcm_sdk_messaging_options {
    uint32_t size;
    const char *api_key, *sender_id, *android_package, *certificate_sha1;
} fcm_sdk_messaging_options;
typedef struct fcm_sdk_buffer { uint8_t *data; size_t size; } fcm_sdk_buffer;
enum fcm_sdk_event_kind {
    FCM_SDK_MESSAGE = 1, FCM_SDK_BACKGROUND_OBSERVER = 2,
    FCM_SDK_NOTIFICATION_OPENED = 3
};
/* JSON is UTF-8 public RemoteMessage data, borrowed only during this call.
 * Return exactly S_OK after successful processing; other results keep work
 * pending. Callbacks run without state locks, after durable transport accept.
 * They may call other SDK APIs. An asynchronous engine must wait for its result
 * under the host's active deferral or return S_FALSE and arrange resumption. */
typedef HRESULT (*fcm_sdk_message_callback)(void *context, fcm_sdk_handle client,
    int kind, int64_t receipt, const uint8_t *json, size_t length);
typedef int (*fcm_sdk_cancel_callback)(void *context);
typedef struct fcm_sdk_callbacks {
    uint32_t size;
    void *context;
    fcm_sdk_message_callback message;
    fcm_sdk_cancel_callback canceled;
} fcm_sdk_callbacks;
typedef struct fcm_sdk_drain_result {
    uint32_t visited, completed_stages, failed_stages;
    int busy, canceled, more_pending;
} fcm_sdk_drain_result;
enum fcm_sdk_background_kind { FCM_SDK_RECOVERY = 1, FCM_SDK_DELIVERY = 2 };
typedef struct fcm_sdk_background_cancellation {
    const void *token;
    int (*is_canceled)(const void *token);
} fcm_sdk_background_cancellation;
typedef struct fcm_sdk_background_callbacks {
    uint32_t size;
    fcm_sdk_callbacks callbacks;
    /* Ownership of callbacks.context transfers only on S_OK from start.
     * Completion then release run on the SDK worker before deferral completion.
     * Context must not be a temporary/stack object. Neither hook may throw. */
    void (*completed)(void *context, HRESULT result, const fcm_sdk_drain_result *drain);
    void (*release)(void *context);
    /* Optional worker-start hook. Copy the value, not this borrowed struct.
     * Token remains valid until context release. Long-running callbacks should
     * poll it to stop waiting when Windows cancels their task. */
    void (*started)(void *context, const fcm_sdk_background_cancellation *cancellation);
} fcm_sdk_background_callbacks;

uint32_t fcm_sdk_abi_version(void);
/* Borrowed static SDK error name, or NULL for an unmapped Windows/transport
 * HRESULT. Copy before unloading the Framework. Never returns a server body. */
const char *fcm_sdk_error_name(HRESULT result);
/* Receiver/configuration open defers automatic registration even when saved
 * network options exist. This permits Core defaults to be restored before the
 * host publishes network initialization through open_with_options. */
HRESULT fcm_sdk_open(const fcm_sdk_configuration *configuration, fcm_sdk_handle *client);
/* ABI21: bind the host Firebase Core app identity independently of the Android
 * FIS/FCM app identity in configuration.app_id. Both belong to project_id.
 * core_app_id must be nonempty. NULL options opens receiver-only and defers
 * automatic registration; non-NULL options behaves like open_with_options.
 * Binding is durable, restored by open_saved, and conflicting rebinding fails.
 * Legacy open functions preserve an existing binding. Core defaults/imports
 * use core_app_id; credentials, token, topics and receipts retain app_id.
 * No old Core values are implicitly moved between distinct app identities. */
HRESULT fcm_sdk_open_with_core_identity(const fcm_sdk_configuration *configuration,
    const char *core_app_id, const fcm_sdk_messaging_options *options, fcm_sdk_handle *client);
/* Saves the network options together with the app descriptor before publishing
 * a client. Strings are copied. Existing receiver-only descriptors may acquire
 * options once; conflicting options for an initialized app are rejected.
 * A later receiver-only open preserves saved options. This does not itself
 * claim that registration/token initialization succeeded. Restoring earlier
 * durable topic work may resume its asynchronous SDK dispatcher. */
HRESULT fcm_sdk_open_with_options(const fcm_sdk_configuration *configuration,
    const fcm_sdk_messaging_options *options, fcm_sdk_handle *client);
/* Worker-only restoration of a configuration previously committed by open.
 * S_FALSE means not initialized; never fabricates options from a message. */
HRESULT fcm_sdk_open_saved(const char *app_name, fcm_sdk_handle *client);
/* Close invalidates the handle; calls already in progress retain their state.
 * Callback contexts remain owned by their individual synchronous drain call. */
HRESULT fcm_sdk_close(fcm_sdk_handle client);
/* Blocking WinRT worker operation. Acquires/refreshes FIS credentials, asks the
 * Hub to register the caller's route, commits token state, then returns UTF-8
 * token bytes. Requires open_with_options (or saved full options). Concurrent
 * SDK registration within this package waits on a cancellable exclusion (up
 * to 90 seconds); no callbacks run here. The caller
 * must free the secret result with buffer_free and must never log its contents.
 * See start_operation for asynchronous completion. In-flight cancellation,
 * token events and topic reconciliation remain integration gates; do not call
 * this synchronous form from a UI/isolate thread. */
HRESULT fcm_sdk_get_token(fcm_sdk_handle client, fcm_sdk_buffer *token);
/* Blocking worker operation under the same registration exclusion. Removes
 * the caller/sender route at Google/Hub before clearing cached token/FID.
 * Keeps the Firebase Installation, user preferences and desired topics.
 * Failure never clears a still-usable local token or reports completed work. */
HRESULT fcm_sdk_delete_token(fcm_sdk_handle client);
/* Effective auto-init: persisted messaging override, then signed package
 * firebase-messaging-defaults.json autoInitEnabled, then the Firebase Core
 * data-collection default (true when absent). No notification permission UI.
 * Get/set do not require network options; receiver-only open can configure
 * defaults before open_with_options publishes automatic network work. */
HRESULT fcm_sdk_get_auto_init_enabled(fcm_sdk_handle client,int *enabled);
/* Persists the override. True schedules SDK registration/refresh asynchronously.
 * False stops future automatic registration, preserving token, pending events
 * and topic intents. An in-flight registration may still finish. Explicit
 * getToken/deleteToken/topic operations remain available. Successful delete
 * suppresses automatic recreation until the next process startup or explicit
 * enable/getToken. Completion of set is not registration success. */
HRESULT fcm_sdk_set_auto_init_enabled(fcm_sdk_handle client,int enabled);
/* Firebase Core's default, independent of messaging auto-init overrides:
 * saved explicit Core value, then signed dataCollectionDefaultEnabled, then true.
 * Receiver-only handles are valid. Getter performs no write/network work.
 * An unset bridge must read this value instead of persisting its own true.
 * NULL getter output returns E_POINTER. */
HRESULT fcm_sdk_get_data_collection_default_enabled(fcm_sdk_handle client,int *enabled);
/* Firebase Core bridge supplies explicit Core changes here before
 * network options and when that Core setting changes. SDK persists them for
 * saved/background opens; explicit messaging/package auto-init overrides win. */
HRESULT fcm_sdk_set_data_collection_default_enabled(fcm_sdk_handle client,int enabled);
/* Atomically import a legacy saved Core bool only when no saved SDK Core value
 * exists. The SDK value itself is the checkpoint: retry after a crash never
 * replaces a newer SDK value. S_OK imported, S_FALSE retained an existing value;
 * both return the resolved Core bool in *enabled. No write on S_FALSE.
 * An inherited/new Core bridge uses the getter instead. Receiver-only is valid.
 * NULL output is E_POINTER; legacy_enabled must be 0/1. */
HRESULT fcm_sdk_import_data_collection_default_enabled(fcm_sdk_handle client,
    int legacy_enabled,int *enabled);
/* Delivery metrics are opt-out by default, with an optional signed package
 * default. Explicit runtime policy is durable and wins after restart. Setter
 * success means policy committed, not remote acceptance. No Google HTTP runs
 * in accept; SDK owns receipt capture, export retries and background recovery.
 * Receiver-only handles are valid. Values must be 0/1; null getter E_POINTER. */
HRESULT fcm_sdk_get_delivery_metrics_export_enabled(fcm_sdk_handle client,int *enabled);
HRESULT fcm_sdk_set_delivery_metrics_export_enabled(fcm_sdk_handle client,int enabled);
typedef uint64_t fcm_sdk_token_subscription;
typedef struct fcm_sdk_token_callbacks {
    uint32_t size;
    void *context;
    /* Borrowed token bytes, SDK WinRT worker, outside SDK/state locks. S_OK
     * accepts native custody; copy/persist before scheduling engine work.
     * Other results retain the latest token event for later replay. */
    HRESULT (*received)(void *context,fcm_sdk_token_subscription subscription,
        int64_t generation,const uint8_t *token,size_t length);
    void (*release)(void *context);
} fcm_sdk_token_callbacks;
/* S_OK transfers one context reference, retained independently of Close(client).
 * Events may begin before subscribe returns. Delivery runs in the foreground or
 * an SDK background pass protected by an OS deferral. Pending latest-token
 * state survives process death; accepted events are not historical cache replay.
 * Concurrent subscriptions in this process each accept it before durable ACK.
 * No network registration/retry policy belongs in the engine callback. */
HRESULT fcm_sdk_subscribe_token_refresh(fcm_sdk_handle client,
    const fcm_sdk_token_callbacks *callbacks,fcm_sdk_token_subscription *subscription);
/* S_OK requests detach. One callback may already be in flight; context stays
 * owned until SDK release, exactly once, on its worker and outside locks.
 * Safe from received itself. S_FALSE means unknown/already detached. No joins.
 * No token callback can run after release. Unsubscribe does not delete a token
 * or consume an unaccepted event. An already running received that returns S_OK
 * may commit its acceptance even if unsubscribe raced. */
HRESULT fcm_sdk_unsubscribe_token_refresh(fcm_sdk_token_subscription subscription);
typedef uint64_t fcm_sdk_operation;
enum fcm_sdk_operation_kind {FCM_SDK_GET_TOKEN=1,FCM_SDK_DELETE_TOKEN=2,FCM_SDK_SYNC_TOPICS=3};
typedef struct fcm_sdk_operation_callbacks {
    uint32_t size;
    void *context;
    /* Worker callback; token bytes are borrowed until this callback returns.
     * Copy them before scheduling engine/UI work. Delete/error has no bytes. */
    void (*completed)(void *context,HRESULT result,const fcm_sdk_buffer *token);
    void (*release)(void *context);
} fcm_sdk_operation_callbacks;
/* S_OK transfers one context reference: completed then release run exactly
 * once on an SDK WinRT worker, outside SDK locks, possibly before start returns.
 * Other start results retain context ownership with the caller. Close(client)
 * does not cancel an accepted operation; it retains its captured client.
 * Context/modules must remain alive until release, even after isolate teardown.
 * Hooks must not throw. Background callers retain their own OS execution scope.
 * Initializing asynchronous dispatch makes the Framework DLL process-resident:
 * SDK retains one LoadPackagedLibrary reference until process termination.
 * Callers retain their own loader reference through each API return, then may
 * release their own references in release; SDK worker epilogues stay resident.
 * Operations retire automatically; there is no operation-close API. */
HRESULT fcm_sdk_start_operation(fcm_sdk_handle client,int kind,
    const fcm_sdk_operation_callbacks *callbacks,fcm_sdk_operation *operation);
/* ABI22: asynchronous foreground/startup replay on the SDK's WinRT worker.
 * The SDK observes OS visibility; no task instance, deferral, permission or
 * registration is fabricated. The caller retains its OS execution scope.
 * S_OK transfers context ownership, with completed then release exactly once,
 * possibly before start returns. Other results retain caller ownership.
 * completed/release are required; started supplies a borrowed SDK cancellation
 * token valid until release. Existing operation cancellation APIs apply.
 * Message callbacks receive an SDK-owned borrowed client handle, valid even
 * after Close(original_client). Do not close or retain that borrowed handle.
 * Callbacks run outside storage locks; an engine may post work to its own
 * dispatcher and return the actual processing result to this SDK worker.
 * Partial/pending results use the same durable drain contract as sync drain. */
HRESULT fcm_sdk_start_drain(fcm_sdk_handle client,
    const fcm_sdk_background_callbacks *callbacks,uint32_t limit,fcm_sdk_operation *operation);
/* Queued-only cancellation. S_OK prevents queued work and completes with
 * ERROR_CANCELLED; S_FALSE means running/finished/already canceled/unknown and does not cancel
 * an in-flight RPC. Topic Futures always return S_FALSE here. Neither result
 * releases context early. Always await release. */
HRESULT fcm_sdk_cancel_operation(fcm_sdk_operation operation);
/* Best-effort cancellation of queued or running work. S_OK records a request;
 * SDK WinRT waits ask their async operation to cancel. Completion reports the
 * actual outcome (successful completion may win the race). Remote effects may
 * already have committed. Context stays owned until completed then release. */
HRESULT fcm_sdk_request_operation_cancellation(fcm_sdk_operation operation);
/* SDK-owned subscribe/unsubscribe Future: S_OK transfers one context reference
 * after durable intent acceptance. Completion/release run exactly once on the
 * shared SDK topic worker, only after its terminal server result (or an actual
 * local error/cancellation). A retry remains pending across backoff; no polling
 * or retry policy belongs in the engine adapter. Close(client) is safe.
 * Queued-only cancel_operation is unsupported (S_FALSE). Best-effort cancellation
 * detaches this observation; it DOES NOT undo the durable intent or unsubscribe.
 * Known terminal results win cancellation races. Await release in every case. */
HRESULT fcm_sdk_start_topic_operation(fcm_sdk_handle client,const char *topic,int subscribe,
    const fcm_sdk_operation_callbacks *callbacks,fcm_sdk_operation *operation);
/* Durable topic intents. S_OK from enqueue means persisted, NOT subscribed.
 * Normalizes the deprecated /topics/ prefix and validates Android topic names.
 * IDs are scoped to the Firebase app and survive process death. */
HRESULT fcm_sdk_enqueue_topic(fcm_sdk_handle client,const char *topic,int subscribe,int64_t *request);
/* status: 0 pending, 1 remotely successful, 2 permanently rejected. S_FALSE
 * means unknown/forgotten ID. Error is the last transport HRESULT; due is UTC
 * milliseconds for the pending head. No token or topic bytes are returned. */
HRESULT fcm_sdk_get_topic_status(fcm_sdk_handle client,int64_t request,
    int *status,HRESULT *error,int64_t *due_ms);
/* Only terminal results may be forgotten after the caller consumes them. */
HRESULT fcm_sdk_forget_topic(fcm_sdk_handle client,int64_t request);
/* Blocking SDK worker, NOT a UI/isolate operation. Processes at most limit
 * ordered requests using current FIS/Hub registration. S_FALSE means pending
 * or retry-deferred; it never means the queued request completed. A known remote
 * success is committed even if cancellation races afterward. Use async operation
 * SYNC_TOPICS for a nonblocking bounded pass, then inspect per-request status.
 * Foreground retries and topic Future contexts are owned by the SDK dispatcher. */
HRESULT fcm_sdk_sync_topics(fcm_sdk_handle client,uint32_t limit,
    uint32_t *processed,int64_t *next_due_ms);
/* Call once on EACH real UI view's thread after its CoreWindow is created,
 * before delivery/startup draining. SDK subscribes to actual Windows visibility
 * and Closed events; repeated attachment is idempotent. Does not initialize
 * Firebase or require options. Pins the Framework for live event callbacks.
 * Background-only activations must not call this or create/activate a window. */
HRESULT fcm_sdk_attach_current_view(void);
/* Queries the process's complete CoreApplication.Views against the SDK's OS
 * observations, not the calling worker's window. No views means background.
 * Any untracked view returns ERROR_NOT_READY, never a guessed background state.
 * The automatic delivery path uses this state before committing its work plan.
 * No UI dispatcher wait, engine callback or network operation runs here. */
HRESULT fcm_sdk_get_foreground_state(int *foreground);
/* Copies borrowed Hub data and commits acceptance. Never invokes an app/engine
 * callback or presents a notification. Requires host recovery activation wiring
 * before use as the Hub ACK boundary. Registration alone is insufficient. */
HRESULT fcm_sdk_accept(fcm_sdk_handle client, const wpn_fcm_message *message,
                       int foreground, int64_t *receipt);
/* Invoke AFTER the transport response, under its remaining deferral, and at
 * startup/recovery. S_FALSE means busy/canceled/failed/remaining work. */
HRESULT fcm_sdk_drain(fcm_sdk_handle client, const fcm_sdk_callbacks *callbacks,
    int foreground, uint32_t limit, fcm_sdk_drain_result *result);
HRESULT fcm_sdk_background_status(fcm_sdk_handle client, int *access, int *registered);
/* Actual per-caller ToastNotifier.Setting, independent of background access
 * and Firebase app initialization. All Firebase apps in a Windows package
 * share this OS setting.
 * Only supported_fields may be projected as supported/enabled/disabled values.
 * Apple-only fields are unsupported; never infer them from authorization.
 * Unknown future Windows values retain their raw value and unsupported auth.
 * Initialize output.size. Both calls are read-only; Windows has no SDK prompt.
 * request reports unsupported option bits and returns the current OS state. */
HRESULT fcm_sdk_get_notification_settings(fcm_sdk_notification_settings *settings);
HRESULT fcm_sdk_request_notification_permission(uint32_t requested_fields,
    fcm_sdk_notification_settings *settings);
/* Lookup returns S_FALSE for unknown IDs/activation strings. It does not queue
 * a tap, mark opened or emit an event; use queue_activation for a real OS tap.
 * Each successful output buffer must be freed by fcm_sdk_buffer_free. */
HRESULT fcm_sdk_lookup(fcm_sdk_handle client, int64_t receipt, fcm_sdk_buffer *json);
HRESULT fcm_sdk_lookup_activation(fcm_sdk_handle client, const char *launch,
    int64_t *receipt, fcm_sdk_buffer *json);
/* Persist an OS tap before engine startup/dispatch. Accepts modern and legacy
 * launch strings. S_FALSE means unknown/already processed; no callback runs.
 * Drain emits NOTIFICATION_OPENED and marks opened only on callback S_OK.
 * The host must call this from its real activation path; it is not a URI or
 * manifest registration API. */
HRESULT fcm_sdk_queue_activation(fcm_sdk_handle client, const char *launch,
    int64_t *receipt);
/* Call synchronously inside BackgroundActivated/IBackgroundTask.Run with its
 * real task instance. Captures a deferral/cancellation before queuing a worker.
 * RECOVERY returns S_FALSE for unrelated task names (no context ownership).
 * DELIVERY authenticates the Hub caller, persists before responding, then drains
 * after that response while the outer deferral remains active. This overload
 * uses an existing client; use start_background_saved for configuration restore. */
HRESULT fcm_sdk_start_background(fcm_sdk_handle client, void *task_instance,
    int kind, const fcm_sdk_background_callbacks *callbacks, uint32_t limit);
/* Acquires the deferral immediately, restores the named SDK app on its worker,
 * and closes its temporary client after context release. Callback client handles
 * are borrowed until release; obtain your own with open_saved for later use.
 * No Dart options are needed. Requires prior successful SDK open/initialization.
 * Missing saved options are reported to completed as ERROR_NOT_FOUND. Host
 * event routing, engine startup and legacy configuration migration remain needed. */
HRESULT fcm_sdk_start_background_saved(const char *app_name, void *task_instance,
    int kind, const fcm_sdk_background_callbacks *callbacks, uint32_t limit);
void fcm_sdk_buffer_free(fcm_sdk_buffer *buffer);
#ifdef __cplusplus
}
#endif
#endif
