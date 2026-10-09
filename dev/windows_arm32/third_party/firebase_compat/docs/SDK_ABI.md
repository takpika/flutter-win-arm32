# Firebase Framework C ABI

Framework: `MacMobile.WpnRuntime.1`, Publisher `CN=MacMobileDev`.
Firebase ABI: `0x00000016` (22). OS ABI: `0x00010005`.
Use the MinVersion in the consumer artifact's `PackageDependency.xml` and the
headers/import library from the same signed build. Link `wpnotify.lib`; Windows
loads `wpnotify.dll` from the Framework. Install the hidden Hub Main package too.
A Framework dependency does not deploy Hub or install the app's lifecycle hooks.

Runtime/Hub38 established an integration baseline. Actual Phone87 FCM data and
notification delivery passed with the app process absent, direct background
activation, Windows notification history and preserved prior state. Phone98
passed foreground loss/replay, native C ABI, asynchronous drain, background
cancellation, DLL lifetime and twelve cold notification resource phases.
The isolated native FIS component passed create/auth/error/commit/delete/recreate
checks. This component result does not cover every Framework facade lifecycle.
Evidence is recorded in `logs/firebase-compat-release-gates-20261016.json`.

Runtime51 is the current development candidate. Completed pure-data receipts
keep a ten-message duplicate window after all work is acknowledged; pending work
and unconsumed notifications are preserved. Runtime40 passed real data/notification delivery
with the canonical server message ID. Runtime41 additionally preserves incoming
millisecond timestamps and excludes the transport routing category from public
messages. Its signed Framework passed native C ABI, metadata and completed-data
retention checks on Phone98, with test packages, stage and lease cleaned.
ABI22 and function signatures are unchanged; the wire timestamp alias preserves
size, alignment and offsets on ARM and host.

Runtime46 passed repeated and concurrent initialization without replacing the
brokered connection. Runtime47 limits task re-registration to an actual
EnableTransferOwnership registration error; its signed packages passed real
cold FCM data/notification delivery on Phone88. Runtime48 preserves a newer
broker-owned connection when an obsolete SocketClosed event arrives. A real
Phone98 comparison covers normal close recovery and the delayed-close race;
the same signed Runtime48/Hub48 also passed actual stopped-app FCM data and
notification delivery with Windows history and normal task/Hub lease completion
on Phone98. These changes keep the
SDK implementation and ABI unchanged from Runtime41.

Runtime48 completed an original Flutter 24-second background Future during
automatic receipt replay. Diagnostic Runtime49 separately traced an interrupted
initial delivery to the Hub's real SocketActivity execution-time limit;
uncompleted stages stayed pending. This does not provide a fresh execution
budget for each message.

Runtime50 keeps an accepted App Service connection until service completion or
real caller-task cancellation; the initial RPC wait remains bounded. Phone87
verified native receiver work for 28.047 seconds with actual caller foreground0
and normal OS/connection completion. Actual signed Runtime50/Hub50 also passed
cold FCM data and notification delivery with Windows history, normal Hub lease
closure, and preserved earlier notification receipts and auto-init policy.
This preserves the public ABI and does not extend an OS activation's budget.

Network transitions, Flutter production integration and real notification taps
remain verification gates. Unopened notification payloads and tag aliases remain unbounded. Custom audio
resource URI/XML verification does not prove audible Mobile playback. The
isolated FIS component does not cover every Framework facade lifecycle. This is
a development SDK, not a complete integration release.

OS view attachment, BackgroundActivated forwarding, real task deferrals and
native host lifecycle wakes are required. See [MIGRATION.md](MIGRATION.md) for
ownership, update ordering and state preservation. The contracts below describe
the current API; older per-candidate evidence does not supersede the baseline
and remaining checks above.

## Received message metadata

The public messageId is the server's opaque persistent message identifier;
transport stanza IDs are only a fallback when no persistent ID exists. App-data
keys cannot override that transport identity. Transport ACK/deduplication keeps
its original identifiers.

sentTime is an epoch-millisecond integer, including subsecond precision. The
borrowed C wire structure retains its historical sent_seconds member as a
same-layout alias of sent_milliseconds; both refer to the inbound raw millisecond
value. TTL remains seconds. The Android routing package is not a public Apple
notification category. Developer data named category remains developer data.

Runtime41 implements this projection without changing function signatures or
message size/alignment/offsets. Previously accepted payloads and private engine
caches keep their original contents; an upgrade does not renumber receipts or
rewrite an application's previously observed metadata.

## Separate Core and Android messaging identities (ABI21)

`fcm_sdk_open_with_core_identity(configuration, core_app_id, options, &handle)`
keeps `configuration.app_id` as the Android Firebase App ID for FIS, FCM,
credentials, topics, receipts and legacy Messaging migration. `core_app_id`
is the host's Firebase Core App ID in the same project. Pass NULL `options`
for the receiver/configuration phase, import the saved Core default through
the atomic import API, and only then open with messaging options to publish
automatic registration. Use this entry point from the first SDK open when
the two IDs differ; quiesce old settings writers before binding an existing app.

The binding commits with the saved application descriptor and is restored by
`open_saved`, including cold background activation. An explicit conflicting
binding is rejected without replacing it. Older open APIs preserve a binding
already saved by this entry point. Core getters/setters/imports and inherited
Messaging auto-init consult the Core scope; explicit Messaging auto-init still
takes priority. Existing Core values under another App ID are retained, never
implicitly copied into the new identity. Flutter imports its saved Windows Core
value explicitly; it owns resource-management and provenance metadata.

No existing C structure or function signature changes. ABI21 adds one export.
Host tests cover split scopes, restart, atomic import retry/rollback, retained
Messaging credentials/topics/cooldown and immutable binding. The C-only native
probe passed on Phone 98 using the actual Framework DLL. Evidence:
`logs/firebase-compat-core-identity29-native-phone98-20261016.json`. Its two
synthetic consumer packages and transfer directory were removed and the lease
released. This does not establish real Flutter delivery or cold OS activation.

## Cold background visibility (Runtime30, ABI21 unchanged)

On Phone 98 a normal App Service cold launch exposed one CoreApplicationView
with no CoreWindow, before the host's SetWindow hook ran. Runtime29 rejected
that view with `ERROR_NOT_READY`. Runtime30 queries the OS window for an
unregistered view and treats an explicitly successful null window result as
not visible. An OS query failure or a real unregistered window still fails;
the SDK does not guess visibility or manufacture a UI window.

The same cold fixture now returns S_OK/foreground=false with WINDOW_CREATED=0
using Runtime30's actual DLL. The existing UI visibility and C ABI suite also
passes. Evidence: `logs/firebase-compat-cold-visibility30-native-phone98-20261016.json`.

## Asynchronous foreground/startup drain (ABI22)

`fcm_sdk_start_drain(client, &callbacks, limit, &operation)` reuses
`fcm_sdk_background_callbacks`. It accepts no task instance or foreground flag.
The SDK's WinRT worker observes the real OS view state and runs the existing
durable drain. It obtains no OS deferral. The host retains its execution scope.

The foreground facade captures the lifetime of the process's OS-observed visible
views when queued, and verifies the full view state again before dispatch.
Losing the last visible view cancels that operation, including its borrowed
native cancellation token. Showing a view again does not revive it: a new drain
replays uncommitted work. An engine reply received after cancellation is not
acknowledged. Actual background delivery must use `start_background` with its
real Windows task instance; the foreground facade never acquires a background
execution entitlement. Runtime38 passed the actual OS-hide, canceled late reply and replay test;
its pending stages were retained and completion/release each ran once.

On S_OK the SDK captures the client and owns one callback context reference.
`completed` and `release` are required and run once, in that order, outside SDK
locks, possibly before start returns. Other start results leave ownership with
the caller and zero the operation output. A serialized engine dispatcher may
enqueue a Dart call and deliver its response while the SDK worker waits, since
the initiating dispatcher is not blocked by this API.

The receipt worker exclusion is released before `completed`. The operation is
also retired from the SDK operation table before that callback. A host may post
a new drain from `completed` without waiting for `release`; another independent
background drain can still make it busy. Copy the borrowed result before posting
it and use a host-owned valid client for the next operation. The prior callback
context, client, and cancellation token must not be borrowed past their release.

This one-shot operation does not retain a host receipt callback and automatically
restart it on UI return. SDK visibility subscriptions cancel stale operations
and wake SDK token/topic/metrics maintenance. The engine adapter owns its actual
OS lifecycle subscriptions and posts a fresh drain after visibility/resume.
Coalesce those wakes without losing one received during an outstanding drain;
carry it into a new operation after completion. Defer a visibility wake through
the UI dispatcher so OS event handlers can update the SDK view observation first.
Eligibility, foreground classification and presentation remain SDK decisions.
Do not supply fabricated visibility, create a deferral, or busy-loop on a
canceled/not-ready result. Real background activation keeps its task-based path.

Message callbacks receive an SDK-owned borrowed client handle valid through
context release, even if the original handle was closed. It may be used for
reentrant SDK calls but must not be closed or retained. `started` optionally
receives a borrowed native cancellation token. Its lifetime ends at release;
the existing caller `canceled` hook can additionally cancel work.

The existing queued-only and running operation cancellation APIs apply.
Cancellation reports ERROR_CANCELLED with `drain.canceled`; uncommitted work
remains pending. A request arriving after a successful commit cannot undo that
commit. Statistics on a failure before drain do not assert that the queue is
empty. Operations retire before completion and have no close API.

This adds one export and changes no existing C structure. The worker uses the
same operation ownership, retirement and process-resident DLL lifetime as the
existing asynchronous token operations. The signed native probe exercises an
engine-reply handshake, original-client closure, reentrancy, cancellation and
durable replay; its device result is still required.

Call on a WinRT-initialized worker. `fcm_sdk_open` selects the Firebase app
name/project/app identity and opens package-private durable state. All Firebase
instances in one Windows package share the store and worker exclusion gate;
each operation is scoped to its Firebase app identity. All instances must select
the same background entry point. NULL/empty registers in-process recovery tasks,
which the app host still must route.

`fcm_sdk_open_with_options` additionally copies and atomically saves API key,
sender ID, original Android package and certificate SHA-1. Project/app identity
comes from the configuration. A receiver-only descriptor can acquire these
options once. An identical reopen is idempotent, a conflicting initialized
configuration fails, and receiver-only opens preserve previously saved options.
Saved restoration loads these options without engine-provided data. Options are
package-private and must not be logged. This API does not claim a successful FIS/token initialization; restoring prior
durable topic work may resume its asynchronous dispatcher. Public asynchronous
token/topic operations and populated legacy options import remain required.

`fcm_sdk_get_token` is a blocking, WinRT-worker-only operation. It restores or
creates/refreshes the FIS installation through Windows HTTPS, asks the Hub to
register the caller's route and commits token/FID preferences before returning
SDK-owned token bytes. Release them with buffer_free; never log the contents.
A separate package-private registration worker handle excludes concurrent SDK
registration operations without holding receipt state locks over network work.
Busy returns ERROR_BUSY; missing full options returns ERROR_NOT_READY.
Phone 87 verified FIS HTTP 200, token state/file agreement, process exit and
restart, a second successful registration and equal token hashes. The second
process used saved FIS credentials without another FIS HTTP request.

The Runtime38 FIS component also passed eight phases on Phone98 (OS14393.2551):
local FID/create, cached/forced auth, client reload from durable credentials,
HTTP success with rejected local commit and successful retry, actual HTTP400
auth/delete failures preserving credentials, delete, distinct-FID recreation
and remote cleanup. Evidence:
`logs/firebase-compat-fis-network-phone98-20261016-v7.json`.
This diagnostic compiles the product FIS/Windows HTTPS sources into an isolated
ordinary app with internetClient. It does not establish Framework facade,
actual process restart, cancellation or Flutter coverage. An earlier fixture
omitted internetClient and failed before an HTTP status; its state was preserved
separately. The corrected fixture, transfer stage and lease were cleaned.

Native registration does not by itself prove push delivery. Subsequent native
SDK candidates separately verified automatic token recreation/refresh, topic
subscribe/unsubscribe, metrics export, absent-Hub startup and real background
FCM delivery. Their scopes are recorded in the release ledger. Real running
network cancellation and full Framework FIS error lifecycle remain separate
checks.

`fcm_sdk_accept` copies a borrowed `wpn_fcm_message` and commits its pending work
before returning its ID. It performs no app callback, notification presentation
or HTTP call. A duplicate returns the original ID and plan. Transport ACK may
follow successful acceptance only once recovery activation is integrated.

Call `fcm_sdk_drain` after the transport response, under the remaining deferral,
and on startup/recovery. The SDK presents standard notifications through Windows.
Callbacks receive UTF-8 JSON public RemoteMessage data borrowed during the call.
FCM_SDK_MESSAGE is the ordinary Android FirebaseMessagingService.onMessageReceived
handler. It includes data received in BOTH foreground and background; event kind
is not an OS visibility flag. A Flutter bridge routes this callback using the
SDK's fcm_sdk_get_foreground_state, sending background data through the registered
background callback Future. Treat query failures as deferred work, not a guessed
foreground/background value. The separately requested FCM_SDK_BACKGROUND_OBSERVER
observes background notifications for an engine.
Foreground notification messages follow Android Firebase Messaging: deliver the
message callback without automatically presenting a toast. FlutterFire's
`setForegroundNotificationPresentationOptions` is an Apple-only setting; its
[method-channel implementation](https://github.com/firebase/flutterfire/blob/main/packages/firebase_messaging/firebase_messaging_platform_interface/lib/src/method_channel/method_channel_messaging.dart)
returns without a native call on other platforms. It does not require a Windows
SDK presentation setter. Legacy saved foreground flags remain migration data;
they do not change this dispatch policy or Windows notification permissions.
Reserved transport fields do not become developer data. Return exactly S_OK
after processing; any other result leaves that stage pending. Independent stages
may complete. Cancellation leaves unfinished work pending. Contexts are owned by
the synchronous drain call and never retained afterwards.

Drain returns S_FALSE for busy, canceled, failed or remaining work, with counters
and flags. An HRESULT failure means an API/storage error, never completed work.
Callbacks may call SDK APIs; a nested drain sees the busy worker. Output buffers
must start unused and be released by `fcm_sdk_buffer_free`, including after the
client closes. Never free them with the app's allocator. Close rejects future
operations; already-running calls retain their objects until they return.

`fcm_sdk_lookup_activation` recognizes `firebase-fcm:<hex receipt ID>` and legacy
`flutter-fcm:<hex receipt ID>`. It returns the scoped saved payload without
marking opened or emitting an event. From the real OS activation handler, call
`fcm_sdk_queue_activation` before starting the engine. This atomically persists
openPending, scopes the receipt to the Firebase app, deduplicates pending/already
completed opens and records the presentation implied by the actual tap. Do not
call it merely to inspect a launch string: a tap is evidence the toast was shown.

`lookup_activation` returns a public RemoteMessage, including for an already
handled receipt. Its JSON contains no private opened/consumed flag and must not
be used to decide whether to create a new initial message. Queue the real OS
activation first. S_FALSE creates no new Flutter cache/event; S_OK includes an
already-pending receipt and does not acknowledge custody. In the OPENED callback,
the bridge durably upserts the full message by receipt before returning S_OK.
Replayed callbacks must retain an existing consumed tombstone rather than
recreate a consumed initial message. An OS-time cold/warm classification intent
may be saved before queueing; filling and consuming the full Flutter cache remain
separate from the SDK's pending-open acknowledgement.

Development Runtime39 collects fully acknowledged pure data receipts after a
noncanceled drain, retaining the newest ten per Firebase app as a durable
recent-duplicate window. An application's callback owns any business archive
once it acknowledges delivery. Pending stages, pending opens/metrics,
notification payloads/transport fields and unplanned legacy receipts are never
collected by this data policy. Receipt identifiers remain monotonic across the
whole Windows package. Notification receipts have a separate completion gate:
presentation and every required callback, OPENED handling and delivery metrics
must all be acknowledged. A noncanceled drain retains the newest ten eligible
receipt IDs per Firebase app. Older consumed receipts return S_FALSE from lookup
and cannot recreate an OPENED event. Pending stages/opens/metrics, unopened
notifications and unplanned legacy records remain intact. Tag aliases are not
removed by receipt collection. Candidate Runtime52 performs separate tag
maintenance before tagged presentation: it removes only canonical aliases with
no retained logical-tag reference in the same Firebase app and no matching
native tag in the caller's current Windows notification history. It reads OS
history outside the file lock, then rechecks references in the current state
transaction. A history/maintenance failure leaves presentation available;
unknown alias extension metadata is preserved. Runtime51 has no tag collector.
Unopened payloads have no count/age eviction policy. The native state-file limit
is 16 MiB; an oversized replacement fails before writing or replacing the
committed file. FCM's server TTL and OS history absence do not establish safe
receipt removal.
The unreleased capacity fix stores receipt completion/presentation/open/metrics
flags in a fixed-width private lifecycle word. New receipts reserve it at
acceptance; legacy boolean records remain readable and migrate on a lifecycle
update, which is smaller than the original flags. Open queue/acknowledgement and
existing work/metrics completion do not grow the record at the file quota.
Payloads and transport fields are preserved. This private format requires old
SDK writers to be quiesced and is not readable by Runtime52 or earlier after a
record is migrated. Public C ABI22 and all public layouts remain unchanged.
Local receivedTimeMs is independent of analytics consent and sender sentTime;
legacy receipts can lack this field.

Drain prioritizes FCM_SDK_NOTIFICATION_OPENED events over the ordinary backlog,
using the same exclusive worker. Return S_OK only after the app/engine has
accepted the event; the SDK then atomically marks opened and clears openPending.
Failures, cancellation or process death leave the open pending for the next
drain. A crash after the callback side effect but before its commit can repeat
the event (at-least-once). Flutter's cold-start initial-message caching and
isolate events remain the Flutter bridge's responsibility. The SDK does not
register an OS activation handler on the app's behalf. Real Windows tap routing
through the app host and Flutter remains unverified.

Native resource resolution, remaining notification policy, image/audio rendering,
bounded retention, full migration, complete FIS/token/topic/metrics lifecycle, automatic
OS execution and real FCM/Flutter validation remain release requirements.
Missing resources or failed OS calls keep presentation pending.

## Asynchronous background entry point (candidate)

Call `fcm_sdk_start_background` synchronously from the real host
BackgroundActivated/IBackgroundTask.Run event, passing its actual task instance.
The SDK acquires an outer deferral and subscribes to OS cancellation before
returning, then runs work on its own WinRT worker. RECOVERY filters the exact SDK
task names; unrelated tasks return S_FALSE without taking context ownership.
DELIVERY uses the existing authenticated Hub App Service protocol, persists the
receipt before the response, and drains only after that response has finished.
An App Service rejection must not be treated as accepted SDK work.

On S_OK, the SDK owns one reference to callbacks.context and calls release
exactly once. On any other start result, ownership stays with the caller. The
SDK copies the callback structures, not pointed-to temporary data. Keep callback
code/modules alive until release. The optional started hook receives a
cancellation query/token pair to copy into the context. Long waits for Dart or
other application work must poll it and stop when Windows cancels the task.
The token is valid only until context release. Ordinary callback cancellation
queries are also honored. No thread is forcibly terminated by the SDK.

Completion and release run before the outer deferral completes. Completion's
HRESULT describes SDK work, not a guarantee that the subsequent Windows
deferral completion succeeded; failures completing it are logged separately.
Task/deferral interfaces are held through agile references and resolved on the
worker's apartment. The generic lifetime helpers are OS code; FCM interpretation
and callback dispatch stay in the Firebase SDK.

`fcm_sdk_open` now commits native app descriptors before publishing a client:
app name/project/app ID, background entry point and observer policy. It rejects
a conflicting identity under an existing app name and preserves unrelated SDK
state. `fcm_sdk_open_saved` restores that descriptor on a WinRT worker, returning
S_FALSE when setup has never occurred. Corrupt state is an error, not a reset.
A native activation worker may open this saved client and queue the real OS tap
before Dart/Core initialization. Queue persists openPending independently of
the client lifetime; close the owned temporary client after queueing. Saved
restoration also resumes enabled automatic registration, topics and metrics.
Flutter must persist its OS launch intent first and retain its own full-message
cache/consumption rules. SDK retention must preserve pending opens; OS history
absence alone is not evidence that an unqueued tap can be discarded.

For a cold SDK registry, call `fcm_sdk_start_background_saved` with the app name
and real task instance. It acquires the deferral before returning and performs
state I/O/restoration on its worker, without waiting for Dart options. Callback
handles are borrowed until context release; the SDK closes its temporary handle
after release. Open your own saved client if an engine needs it later. If saved
configuration is absent, completion receives ERROR_NOT_FOUND; no receipt is
acknowledged as accepted. Configuration revisions are checked again before
publishing a restored client.

This requires a prior successful SDK open that saved the descriptor. Existing
legacy configuration import and completely cold process/engine activation still
need validation. No host event handler is installed on behalf of the app. The
C test host uses wpn_app_run_with_background and drops its foreground clients
before triggering saved restoration; that native test passed on Phone 87. Product
engines keep their own host and callback-cache/isolate bindings.

## Token operations (ABI 8)

Both token acquisition and deletion have worker-only synchronous forms and
`fcm_sdk_start_operation`. The start API captures its client before queuing work;
closing that client handle does not invalidate already accepted work. On exact
S_OK it owns one callback-context reference. Completion and release each run
once on an SDK worker, outside SDK locks, possibly before start returns.
On other start results the caller retains ownership. The callback's token buffer
is borrowed only for completion; copy bytes before queuing Dart/UI work. The SDK
wipes/frees its buffer before calling context release. Keep the native context
and callback code alive until release, including after engine teardown.

`fcm_sdk_cancel_operation` accepts cancellation only while queued. Exact S_OK
means the worker will report ERROR_CANCELLED without making network calls.
S_FALSE means no new cancellation was accepted (already running/completed/
canceled/unknown). It does not interrupt RPCs or release context early. Completed
operations retire automatically. Operation callbacks do not install a host OS
deferral; a background caller must retain its existing execution scope while it
awaits completion. In-flight network cancellation remains a release gate.

Acquisition now explicitly initializes the headless Hub transport before FIS/
registration. Deletion uses existing FIS identity/auth for modern registrations
and clears token/FID cache only after Google/Hub success. It preserves the
installation, unrelated SDK state, user settings and desired topics. A missing
FIS installation retains the old Hub registration path; populated legacy
migration/reconciliation is still required. The token fixture reads an explicit
get/delete operation file and can use the existing consumer Identity to preserve
Hub ownership. Native token delete/recreate and refresh callbacks passed in
Runtime23; full Framework FIS lifecycle coverage remains separate.

## Running cancellation and runtime residency (ABI 9)

`fcm_sdk_request_operation_cancellation` requests cancellation for queued or
running work. S_OK records the request, while completion reports the actual
outcome. A successful operation may win the race. The SDK's thread-scoped OS
wait adapter asks a pending WinRT operation to cancel; it also checks before
starting subsequent HTTP/registration steps. Cancellation does not undo server
effects. Accepted Google/Hub results are committed before returning success.
FIS cancellation preserves a pending FID for retry and is not converted into a
generic network error. SDK background workers scope the same wait adapter to
actual task/context cancellation and pop the scope before completion/release.

The Framework is a process-lifetime runtime. Initializing SDK asynchronous
operation/background dispatch retains one LoadPackagedLibrary reference to its
own wpnotify.dll and verifies the loaded image is the executing module. That
reference remains until process termination, including if a subsequent thread
creation fails. This is intentional residency, independent of consumer loader
references and client handles. Closing a client does not unload the runtime.
Callers keep their own module reference through API returns and callback code
through release returning. Consumer release may drop its own final SDK loader
reference: SDK code, C++ cleanup, TLS cleanup and worker return remain mapped.
There is no SDK runtime-unload API.

The dynamic-only probe embeds OS helpers but has no wpnotify.dll static import.
It drops the caller reference after start returns, drops its final consumer
reference inside release, waits for the duplicated SDK worker thread handle to
signal exit, and checks the resident SDK is callable afterwards. PE import-table
verification and actual device execution passed, including Runtime38's native
DLL lifetime checks. Running network cancellation coverage remains separate.

## Windows notification settings (ABI 10)

`fcm_sdk_get_notification_settings` queries the calling package's actual
ToastNotifier.Setting without initializing Firebase state. It preserves the raw
Windows reason and maps Enabled to SDK authorization 2, the four known disabled
reasons to 1, and unknown future reasons to unsupported (-1). These SDK enum
values are not FlutterFire wire values. Engine adapters translate their own
wire contract. Background execution access is queried separately.

`fcm_sdk_request_notification_permission` returns the same current OS state
without a prompt or policy mutation. Only authorization is supported; requested
Apple options are reported in unsupported_requested_fields. Do not synthesize
alert/sound/badge values from package authorization. Initialize output.size;
notification_settings.h is published beside sdk.h. Host mapping tests pass;
Windows query parity before SDK initialization awaits the C API device probe.

## Process visibility (ABI 11)

The UI host calls `fcm_sdk_attach_current_view` on each view's UI thread after
its CoreWindow exists. This attaches SDK-owned VisibilityChanged/Closed
subscriptions, initializes from CoreWindow.Visible and retains the Framework
for callbacks. Repeated calls for the same view are idempotent. Do not call it
from a background worker or create a UI view for a background activation.
The host supplies no Firebase dispatch policy or foreground guesses.

`fcm_sdk_get_foreground_state` enumerates CoreApplication.Views for the process
and combines SDK observations of those actual views. It never uses the current
worker thread's CoreWindow. No views means background; any untracked view means
ERROR_NOT_READY, not a fabricated background result. The host must attach every
created view before acknowledging delivery. Closed unsubscribes on the UI
thread. An incomplete subscription is rolled back, including allocation errors.
No UI dispatcher wait or engine callback occurs in the query/accept path.

Automatic App Service acceptance now uses that process snapshot when committing
the receipt's dispatch plan and preserves the same snapshot for the immediate
drain. Recovery also queries actual process visibility; previously committed
receipt plans remain durable. Explicit accept/drain still support their existing
foreground argument. The C API probe has an independent raw Windows visibility
observer, an MTA polling observer and wrong-thread/idempotence checks. Native
UI-visible, hidden/resumed, suspended and cold-background activation validation
remains required before Flutter switches adapters. In particular, the cold host
must attach any views it creates even if it does not activate their windows.

## Durable topics (ABI 12)

`enqueue_topic` normalizes Android topic names, repairs recovery registration
and commits an app-scoped FIFO request. Its S_OK means persisted, never remote
subscription success. `get_topic_status` distinguishes pending, successful and
rejected results, preserving the last transport HRESULT and retry deadline.
`forget_topic` accepts only terminal requests after their result is consumed.

`sync_topics` is a blocking SDK worker operation; async operation kind
SYNC_TOPICS provides a bounded pass without blocking the UI/isolate. The SDK
acquires cross-process registration exclusion, obtains current FIS/Hub token
credentials and invokes the existing Hub subscribe/unsubscribe transport.
Only a successful remote response commits both completion and confirmed topics.
A crash or failed state commit leaves the original intent for idempotent replay.
Cancellation retains unfinished intent; known remote success is committed even
if cancellation races afterward. No HTTP runs inside a state transaction.

Retries persist their UTC deadline with 30-second exponential backoff capped at
eight hours. A deferred head blocks the tail. Registration failure can defer a
request before its first token exists. A changed token generation prepends
internal replay of confirmed subscriptions before pending user requests, so a
queued unsubscribe cannot be undone by token-refresh replay. Internal replay
results are forgotten after success; user terminal results remain until consumed.

The automatic background entry point attempts one topic request after receipt
draining, after the Hub response, under the same cancellation/OS deferral. It
also runs on recovery activation. Denied OS background access still means
foreground-only recovery. A timer registration does not guarantee a 30-second
wake, nor does it establish automatic foreground scheduling.

Host tests cover FIFO restart, delayed-head ordering, token replay ordering,
failed commits, confirmed-preference atomicity, app isolation, invalid names,
pre-registration retry and corrupt state. Native topic transport is build-only.
The token probe supports topic-sub/topic-unsub/topic-sync against its dedicated
wpnotify-native-probe test topic, and checks request state instead of equating a
successful bounded pass with its own subscription result.

Native asynchronous topic Future/foreground retry validation, typed Google
permanent-error responses/Retry-After, terminal-result retention and real topic
delivery are still required before Flutter changes its adapter. The
Hub currently collapses unrecognized Google registration responses to
ERROR_BAD_NET_RESP; these remain pending for retry rather than fake success.

## Topic Futures and foreground dispatcher (ABI 13)

`start_topic_operation` starts a durable subscribe/unsubscribe Future with the
same completed/release ownership contract as other SDK operations. S_OK is
returned only after committing the intent and publishing its completion context.
A rejected start leaves context ownership with its caller. Closing its client
does not invalidate accepted work. Retry deadlines do not complete the Future. Initial-registration backoff is
respected even before a token exists and when another request wakes the worker.
The SDK completes it only after reading the durable terminal result, or an
actual local error/cancellation. Consumed terminal results are then forgotten.
No application/engine polling loop is required.

One shared process worker services all enrolled Firebase apps. An enrollment
spans the queue commit and callback publication; generation-checked retirement
prevents a worker's older empty-queue observation from discarding a concurrent
request. An observer list is captured before reading its request state, so a
newly published Future cannot be mistaken for a missing request in an older
state snapshot. Callbacks and context release run outside SDK state/registry
locks; exception boundaries keep the native thread entry point intact.

The automatic dispatcher requires the host's SDK visibility attachment and only
runs network work while its process has an observed visible view. OS visibility
changes wake it, and loss of visibility cancels its WinRT waits without dropping
intent. Hidden/suspended work waits for foreground visibility; actual background
activation performs its bounded pass under the existing task deferral and
cancellation, then wakes observers. Restored pending work resumes on foreground
SDK open, saved open or successful token acquisition. This does not grant a
background execution right or manufacture an always-running service.

Queued-only cancellation returns S_FALSE for a topic Future. Best-effort
cancellation releases that observation through completed/release but keeps the
durable subscription intent; it does not perform unsubscribe. A known terminal
result wins a cancellation race. An application requiring unsubscribe must
request that separate operation. Background callers retain their own execution
scope as with other asynchronous SDK operations.

Portable dispatcher tests cover client ownership, concurrent publication and
retirement, stale app-entry replacement and retry decisions. Native topic
subscribe/unsubscribe passed on Runtime24. Full engine Future integration and
secondary-app cold recovery remain separate integration checks. Enqueue
acceptance must never be reported as confirmed server subscription.

## Registration error contracts (Firebase ABI 14 / OS ABI 1.4)

The OS transport adds get_token_ex/delete_token_ex/subscribe_ex/unsubscribe_ex.
Their optional size-checked response carries only raw HTTP failure status and
a bounded backend error label. Metadata is absent for old-Hub/ordinary transport
failures. The Hub does not decide Firebase retry policy and never returns HTTP
response bodies through this interface. Existing calls remain conservative
failures for remote errors; route ownership, token buffers and deletion commit
boundaries remain unchanged.

The SDK maps explicit Google legacy GmsRpc errors to stable HRESULTs in
messaging_error.h. Invalid sender, authentication, parameters, quota and other
explicit server rejections become terminal results. SERVICE_NOT_AVAILABLE and
both internal-server spellings remain retryable. RST is exposed as the distinct
instance-id-reset failure, matching GmsRpc's explicit error rather than an
ordinary transient retry. This does not silently delete a Firebase Installation.
HTTP 408/429/5xx are retryable; bodyless 4xx are terminal. FIS bad-config, rate
limit and availability exceptions use the same SDK error catalog; unrelated
local storage/schema errors retain their failure semantics.

`fcm_sdk_error_name` returns a borrowed static SDK name for catalogued HRESULTs,
or NULL for an unmapped Windows/transport result. Engine bridges translate this
SDK error contract; they do not implement retry policy. No mutable thread-local
last-error string, credentials or server body crosses the ABI.

Seventeen SDK host suites exercise framing/classification, malformed/oversized
labels, transient/permanent/reset decisions and error names. Six ASan/UBSan OS
contract suites execute the actual Hub/client code, including HTTP 503 and
INVALID_SENDER propagation, route preservation and legacy failure behavior.
The standalone OS C ARM app also builds. Native metadata/error-name/Future
verification and Retry-After propagation remain gates.
The current path is the existing legacy GMS registration/topic protocol; it does
not claim coverage of the separate opt-in 2026 V1 registration protocol.

Reference behavior: Firebase's [GmsRpc](https://github.com/firebase/firebase-android-sdk/blob/main/firebase-messaging/src/main/java/com/google/firebase/messaging/GmsRpc.java)
response and retry classification. Network policy belongs to this SDK;
[modern TopicSubscriptionClient](https://github.com/firebase/firebase-android-sdk/blob/main/firebase-messaging/src/main/java/com/google/firebase/messaging/TopicSubscriptionClient.java)
uses a separate SDK HTTP endpoint, so legacy transport tests are not evidence
for that newer endpoint.

## Server retry deadlines (Firebase ABI 15 / OS ABI 1.5)

The HTTPS transport reads Retry-After through the OS response-header map and
normalizes delay-seconds or IMF/RFC850/asctime HTTP dates to a UTC deadline.
Malformed/absent values remain unknown; overflow saturates instead of allowing
an early retry. Header lookup and TLS behavior still require native validation.

Raw OS transport V1 remains a 104-byte response. A V2 embeds that prefix and
adds retry_at_ms; pass &v2.base with base.size=sizeof(v2). Size-aware writes
preserve a V1 caller's size and never overwrite its buffer. RPC carries the
deadline only when available; old REMOTE replies remain understood.

The SDK persists an app-scoped messaging cooldown alongside preferences, and
applies it to token acquisition, deletion and topic sync across new client
handles/process restart. FIS exposes its limiter deadline so a registration
failure before the first token can also persist the server wait. A deferred
head inherits that deadline without counting another failed network attempt.
Known registration success atomically clears its expired cooldown with token
state. Independent GMS receive-transport initialization remains allowed during
registration backoff; receipts and notification presentation do not use this
registration gate.

Partial receipt-only upgrades now merge SDK module records by Firebase app
identity, preserving native-only apps, importing missing apps and rejecting
different overlapping records. Absent retry fields equal their explicit zero
defaults for that comparison. The established legacy fingerprint schema is not
changed, and committed imports still never overwrite later native state.

The test app host now attaches visibility from real UI view creation, including
hidden views created during cold background activation. Attaching only from
Activated leaves that case untracked. Runtime38 verified native cold background
delivery; final engine cold startup/tap routing remains an integration gate.

Seventeen SDK suites and seven production OS contract suites pass with
ASan/UBSan. They cover all HTTP-date forms, the RFC850 fifty-year boundary,
calendar/overflow/truncation/fuzz inputs, V1 buffer canaries, GMS deadline
metadata and FIS-to-persisted-topic cooldown. Signed ARM Framework/probes and
the standalone legacy OS C app build. Native header retrieval, cooldown/restart,
visibility, actual topic delivery and Flutter integration remain gates.
See [RFC 9110 Retry-After](https://www.rfc-editor.org/rfc/rfc9110.html#name-retry-after)
and [Windows response headers](https://learn.microsoft.com/en-us/uwp/api/windows.web.http.headers.httpresponseheadercollection).

Runtime 1.0.0.20 fixes apartment-local CoreApplicationView identity comparison.
The UI attachment retains an IAgileReference; Snapshot resolves that view in
its calling apartment before comparing canonical IUnknown identity to the OS
view enumeration. Closed releases apartment-bound window/view interfaces on
the UI thread even while a worker holds the shared observation state. Unknown
or closed views still defer processing rather than guessing background state.
Native UI/worker visibility and later strict OS-hide cancellation/replay passed.
See Runtime38 native evidence in the release ledger.

StateDocument now rejects raw NUL bytes before parsing, preventing a valid
prefix followed by NUL/suffix from being silently rewritten as valid state.
Escaped JSON \u0000 and UTF-8 remain supported. All seventeen host SDK suites
pass with ASan/UBSan after this change.

### ABI 16 token refresh subscriptions (development)

ABI 16 adds subscribe_token_refresh/unsubscribe_token_refresh with a new
callbacks structure. ABI 15 layouts/signatures stay unchanged; consumers check
runtime >= their required ABI and resolve newly required exports. A callback
gets borrowed token bytes, its subscription ID and the persisted generation.
S_OK accepts native custody, independently of Dart stream attachment. SDK state
and token are committed together. Failed callbacks/commits remain pending;
stale acknowledgements do not consume a replacement token. Existing legacy
cached tokens are adopted without manufacturing an historical refresh event.

Close(client) leaves accepted subscriptions alive. Unsubscribe requests detach;
one received callback may already be running. SDK release follows that callback
exactly once, outside locks, and is the lifetime barrier for engine/context
teardown. Calling unsubscribe within received is supported. Native callbacks
must copy/persist promptly rather than block on Dart delivery. Within a process,
cohort membership is serialized with durable ACK; joining during that commit
starts after it. An event with no accepting subscriber remains available after
restart. Foreground and explicit OS-deferral background passes share the same
claims, preventing simultaneous callbacks to one subscription. No engine retry
timer or SDK-private JSON interpretation is needed.

Provisional permission has an explicit requested bit (8192); Windows returns it
as unsupported with no prompt. The token-listener ARM build and nineteen SDK
ASan/UBSan suites pass, including two hundred competing dispatch claims, late
subscription/ACK ordering, reentrant detach and client lifetime. Native callback
and token replacement/deletion coverage still require their separate evidence.
ABI 17 supplies the auto-init facade. Metrics and the final Flutter integration
remain pending.

### ABI 17 automatic initialization (development)

ABI 17 adds get/set_auto_init_enabled and set_data_collection_default_enabled.
Existing ABI15/16 structures and exports remain unchanged. Effective policy is
persisted messaging override, signed package autoInitEnabled, then inherited
Core data-collection default (true when absent). Optional app-root
firebase-messaging-defaults.json uses version=1 and boolean autoInitEnabled /
dataCollectionDefaultEnabled. Invalid metadata never silently enables automatic
network work. Package path/version come from normal WinRT Package.Current,
without desktop-only package APIs or Interop Unlock.

Receiver-only open defers automatic registration even if full options were
saved before. Core restores its setting there before open_with_options; saved
background opens use the committed SDK policy. Setter success means persisted
policy, not completed registration. Disabling leaves valid tokens, pending
refresh events and desired topics intact; explicit get/delete/topic still work.
SDK serializes explicit get/delete against automatic network work with a
cancellable worker wait. No synchronous network call belongs on the UI/isolate.

Automatic work runs on the SDK dispatcher while visible, or a bounded pass in
an actual OS background deferral after durable receipt acceptance. It validates
missing/stale tokens, seven-day age, consumer package version and changed FIS
identity; retry state survives restart and respects server/FIS cooldown. Hard
errors stop that process's automatic attempt, with a fresh attempt at the next
process startup or explicit enable; successful registration clears its failure
state atomically with the token. Known token changes wake token subscriptions
and topic reconciliation. Internal topic restoration uses an existing token;
absence of a token plus desired topics alone never forces implicit recreation.

Successful deletion suppresses implicit recreation for that SDK process while
preserving desired topics. A new process or explicit enable/get/topic may
create a token again. The SDK retains its module for the deletion-session
marker, so dynamic unloading cannot accidentally bypass that suppression.
Twenty host suites pass with ASan/UBSan, and signed ARM builds pass. On Phone 87,
the probe's auto mode passed SDK delete followed by real token creation without
an explicit get, after Close(client). Its first token callback deferred; the
same generation/value replayed and was accepted with reentrant detach and one
release. Native preferences, dynamic residency and real OS background suites
also passed. Evidence: `logs/firebase-compat-auto23-native-phone87-20261016.json`.
Metrics facade and real receipt export passed on Runtime25. Final Flutter
background/tap/cold integration remains incomplete.

On Phone 98, .22 / ABI16 has separately passed the native subscription context
lifetime, C ABI, dynamic residency and actual OS ApplicationTrigger suites.
Flutter's production native notification-settings bridge/StandardMessageCodec
also passed there without starting Dart/Flutter or changing SDK receipt ACK.
Those proofs must not be attributed to ABI17 scheduling or complete integration.

### ABI 18 delivery metrics (development)

`fcm_sdk_get_delivery_metrics_export_enabled` and
`fcm_sdk_set_delivery_metrics_export_enabled` work with receiver-only handles.
Export defaults to false. The optional signed package property
`deliveryMetricsExportToBigQueryEnabled` supplies a default; an explicit runtime
0/1 policy wins and survives restart. NULL getter output returns E_POINTER;
other integer setter values return E_INVALIDARG. Setter success means policy
committed, not a completed network export.

Acceptance records receive time, uptime, timezone and effective opt-in in the
same receipt commit. No metrics callback or network request runs in accept.
Old receipts and receipts accepted while disabled are not retroactively
exported. After transport response and normal message processing, the SDK
transfers eligible receipts to its durable CCT queue. Queue commit precedes
marking the receipt's metrics stage complete; receipt-scoped deduplication
recovers a crash between those commits. Metrics do not change drain's main
stage counters, transport ACK, notification presentation or engine ownership.

The SDK owns local FID fallback, real gzip compression, TLS through Windows,
bounded allowlisted redirects, NID persistence, backend cooldown and retries.
It runs while visible or under an actual OS background deferral. Cancellation
is observed between captures and during HTTP waits. The sender has independent
cross-process worker exclusion. Existing metrics v1 policy stays explicit;
partial migration normalizes only the comparison, preserving old fingerprints.
Flutter translates its public boolean API and owns no reporting JSON or timer.

Artifact: `build/wpn-runtime-metrics-v3/consumer`. Twenty-four ASan/UBSan host
suites and signed ARM build pass. Phone 87 passed the public metrics facade,
persisted policy, C ABI, dynamic residency, actual OS background activation and
cancellation, native storage and automatic Google registration. Evidence:
`logs/firebase-compat-metrics25-native-phone87-20261016.json`. A real Firebase Console notification then reached Hub/SDK on Phone 87; its
receipt completed and the matching CCT item was accepted by the Google backend
and durably marked sent. Evidence:
`logs/firebase-compat-metrics25-real-receipt-phone87-20261016.json`. BigQuery
ingestion is not verified by that backend acceptance.

### ABI 19 Firebase Core default getter (development)

`fcm_sdk_get_data_collection_default_enabled` reads the Firebase Core default
without applying messaging's runtime or signed `autoInitEnabled` overrides.
It resolves the saved explicit Core setting, then signed
`dataCollectionDefaultEnabled`, then true. Reading an unset value never saves
an override. Receiver-only handles work without messaging network options;
NULL output returns E_POINTER. Existing setter behavior and ABI15 structures
are unchanged.

ABI19 provides the getter for inherited defaults and the setter for explicit
Core changes. Atomic import of an existing saved Core value requires ABI20
below; repeated ordinary setter calls cannot provide that migration boundary. An unset Core bridge reads this getter
instead of writing its own true default. The SDK owns persistence and packaged
default resolution; the engine converts the resulting boolean to its public
Core DTO. A messaging override must not be used to infer the Core value.

This follows Android's `DataCollectionConfigStorage` preference/manifest/true
resolution:
https://github.com/firebase/firebase-android-sdk/blob/main/firebase-common/src/main/java/com/google/firebase/internal/DataCollectionConfigStorage.java
Phone 87 passed the public Core getter and a signed-metadata probe across
two processes: packaged Core=false and messaging=true remain independent;
explicit Core=true and messaging=false survive restart. Evidence:
`logs/firebase-compat-core26-native-phone87-20261016.json`. Flutter integration
is still pending.


### ABI 20 atomic legacy Core import (development)

`fcm_sdk_import_data_collection_default_enabled(client, legacy_enabled, enabled)`
imports a saved old Core boolean only if the SDK has no saved Core value. The
read, conditional update and returned value belong to one SDK transaction.
S_OK means imported; S_FALSE returns an existing SDK value without writing.
An SDK value of false is present, not an absent override. A later explicit SDK
setting always survives a repeated import, including after an engine-marker
commit failed. No separate migration marker or SDK JSON access is required.
NULL output returns E_POINTER; input must be 0/1. Receiver-only handles work.

Flutter keeps legacy provenance, invokes this import for legacy saved values,
uses the getter for new/inherited values and the setter for explicit changes.
An explicit Core value already committed to the SDK wins over legacy state.
Host tests cover retry/restart, changed SDK values, commit failure, scope and
zero writes for an existing value. Phone 87 passed the public import API and
process-restart retry after a newer SDK Core setting. Evidence:
`logs/firebase-compat-checkin27-native-phone87-20261016.json`. Full Flutter
handover is still pending.

## Packaged notification assets (development Runtime38, ABI22 unchanged)

The SDK resolves notification icon and sound names through the calling app's
main MRT Resources map, without a CoreWindow or an engine callback. Resource
builds provide these named strings (RESW dots become MRT path separators):

| RESW name | Value |
| --- | --- |
| `drawable.mail` | `ms-appx:///Assets/mail.png` |
| `mipmap.launcher` | `ms-appx:///Assets/fallback.png` |
| `raw.chime` | `ms-appx:///Assets/chime.wav` |
| `firebase_messaging_default_notification_icon` | `launcher` |

Icon lookup tries drawable then mipmap. If the named resource is missing, the
optional default icon is resolved; otherwise Windows uses the app's own logo.
Missing named sounds use the OS default sound. `default_sound=1/true` overrides
a custom sound, including Java Boolean.parseBoolean case variants. File lookup
uses StorageFile.GetFileFromApplicationUriAsync, so PRI-qualified files can be
resolved without an unqualified physical file. Unexpected OS errors and malformed
packaged URI mappings fail the presentation stage and retain pending work.
The shared OS async wait observes the actual task/foreground cancellation.

These resource mappings belong to the platform resource build. Flutter/Dart and
application callbacks do not perform notification resource lookup or display
policy. Only signed package-local ms-appx URIs are used for this mapping.
The SDK supplies the URI to Windows toast XML; Windows owns volume, quiet mode
and playback. Runtime38 passed twelve cold resource phases on Phone98
(OS 14393.2551), including actual Windows history, localized/raw text,
drawable/mipmap/default/missing files, sound URI/default precedence and full
Unicode uppercase. Evidence: `logs/firebase-compat-uppercase38-native-phone98-20261016-flex1.json`.
The fixture proves package-qualified file resolution at scale100; scale200
selection and a consumer app resource build remain separate integration checks.
Custom audio URI/XML checks do not prove audible playback on Mobile. A modern
Windows App SDK audio example is not proof of Windows Phone behavior.

References: [Firebase Android notification builder](https://github.com/firebase/firebase-android-sdk/blob/main/firebase-messaging/src/main/java/com/google/firebase/messaging/CommonNotificationBuilder.java),
[Windows StorageFile URI resolution](https://learn.microsoft.com/en-us/uwp/api/windows.storage.storagefile.getfilefromapplicationuriasync),
[UWP toast audio schema](https://learn.microsoft.com/en-us/uwp/schemas/tiles/toastschema/element-audio).


Runtime48 also passed the full published-consumer build path on Phone87
(OS14393.2551). The test app uses only consumer48 headers/import library and
manifest fragments, with a new combined PRI generated from the entire app's
RESW/assets by Microsoft MakePRI. Both scale100 and scale200 candidates are
retained. All twelve cold resource/presentation/history phases passed without a
CoreWindow, and the two fixtures/stage/lease were cleaned. Evidence:
`logs/wpn-consumer-resource48-validation-20261017.json`. This does not establish
audible custom-sound playback.
