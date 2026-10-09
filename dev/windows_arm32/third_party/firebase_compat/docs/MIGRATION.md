# Framework integration and migration

The native SDK and hidden Hub are available as signed development packages.
Runtime/Hub51 passed native delivery and opened-notification retention; Flutter's actual Dart/isolate and OS-tap
integration is still being verified. Use the matching consumer artifact rather
than embedding an old SDK implementation or a private DLL.

## Ownership and installation

| Component | Responsibility |
| --- | --- |
| Hub Main | Google checkin, FCM transport and authenticated delivery routing |
| Runtime Framework | OS helpers plus Firebase SDK, executing in each calling app's process, identity and LocalState |
| Firebase SDK | FIS, token/auto-init/topic/metrics, durable receipt stages, default background notification display and pending opens |
| Flutter adapter | Published Dart API/codecs, actual OS lifecycle forwarding, SDK-to-isolate bridge and durable full initial-message cache/consumption |
| Application | Business handlers, backend token upload and custom/foreground notifications |
| Windows | Notification authorization/settings and OS scheduling/execution limits |

Install the signed Runtime Framework and Hub Main. The Framework has no
Applications or Capabilities declaration; Hub is a separate hidden ordinary
Main package. A consumer's PackageDependency deploys neither Hub nor lifecycle
hooks. The consuming app declares its own App Service/background activation
and networking capabilities. internetClientServer includes internetClient.
Framework use does not grant additional rights or require Interop Unlock.

Use headers, import library and PackageDependency.xml from one consumer build.
Require the ABI needed by the functions used, resolve optional exports before
calling them, and accept additive newer ABI versions. Never resize an older
ABI structure or substitute an exact version-equality check.

## Preserve existing state

1. Quiesce every old SDK writer before opening the shared SDK. This includes
   FIS, token, topics, auto-init/Core defaults, receipt completion and metrics.
   Engine callback-handle/cache writers remain engine-owned.
2. Keep `flutter-firebase-state-v1.bin` and Flutter callback/cache state valid.
   Native migration imports the SDK-owned subset into
   `firebase-messaging-state-v1.bin` in one atomic commit. It never edits the
   legacy file or copies the engine's private cache.
3. Bind the Core and Android Firebase App IDs from the first native open when
   they differ. FIS/FCM data stays under the Android app identity; Core settings
   use the saved Core identity. Reject an explicit conflict instead of moving
   credentials or routes to another identity.
4. Import a saved legacy Core default through the atomic import API before
   publishing Messaging options/auto-init. An existing SDK value, including
   false, wins on retries. Engine provenance/resource-management metadata stays
   in the engine store.
5. Retire the old SDK writers only after verifying the new SDK path and the
   original published Dart API. Do not run two implementations against one
   Firebase app's credentials or delivery acknowledgements.

Migration preserves installation revisions/tombstones, FID/auth credentials,
token/FID pairing, topics, preferences/cooldowns, metrics and unrelated module
fields. Inbox IDs and independent complete/presentation/open flags remain
stable. Reserved transport keys are excluded from public developer data.

The canonical SDK-subset SHA-256 checkpoint detects later legacy SDK writes,
including flag-only writes without a sequence change. Engine-only edits and
JSON object/app-array ordering do not count as SDK mutations. The fingerprint
is conflict detection, not coordination of active old workers. Interrupted
commits can retry; a finished import cannot overwrite newer native state.
Receipt-only checkpoints may acquire missing modules, but conflicting modules
fail atomically. The lock order is legacy then current, with no network or
application callback under those state locks.

A populated native migration passed on Runtime30, including BCrypt hashing,
engine-only changes, old-writer rejection and a retained old Windows toast.
Evidence: `logs/firebase-compat-migration21-native-phone98-20261016.json`
records the actual Runtime30 version, phase markers and cleanup.

## Host activation and delivery

Attach each real CoreWindow from its own UI thread at view creation, even when
a background launch never raises Activated. A successful OS query reporting no
window is a cold background view; a query failure or untracked real window must
not be guessed away. Authorization is read from Windows notification settings;
background access is a separate status and no permission dialog is fabricated.

Forward actual CoreApplication.BackgroundActivated and its IBackgroundTask to
`fcm_sdk_start_background_saved` for delivery/recovery. The SDK acquires the real
deferral and observes cancellation. Keep callback context/code alive until
release. Foreground/startup drains use `fcm_sdk_start_drain`; this worker obtains
no background entitlement. The engine forwards real visibility/resume wakes,
coalesces wakes arriving during a previous drain, and posts a new drain after
completion. Dispatch through the UI queue first so OS visibility observers have
settled. A canceled old drain cannot be revived by showing the view again.

SDK acceptance repairs recovery registration, copies the borrowed Hub payload
and atomically saves pending stages before responding to Hub. That response is
durable custody, not display or Dart success. It never waits for presentation,
application callbacks, metrics or another network request. Processing continues
under the existing App Service deferral after the response. Hub retains its
caller connection until service completion or actual caller-task cancellation.
Execution time comes from the real Windows activation, including the caller's
remaining budget; accepting a message does not create a fresh execution budget.

Workers use cross-process exclusion released on process death. State locks are
released before callbacks. Presentation, handler, observer and open completion
are independent; failed/canceled stages remain pending. A crash between a side
effect and commit may replay it, so callbacks have at-least-once semantics and
toast identifiers stay stable. No ApplicationTrigger is requested from a
background task; timer recovery has a minimum 15-minute interval. Repair a
partial registration without removing the previous usable registration first.
OS denial/cancellation is never converted into execution or completion success.

## Notification activation and cache custody

Forward the real raw OS launch argument before engine startup. On a native
worker, restore the saved SDK client and queue the activation before waiting for
Dart/Core initialization. Preserve the engine's cold/warm classification intent.
SDK queueing scopes the receipt, records openPending and deduplicates pending
or completed opens. It does not consume Flutter's initial-message cache.

Both `flutter-fcm:<hex id>` and `firebase-fcm:<hex id>` resolve against their
original IDs. Imported presentation checks the old flutter-fcm history group
and unpadded tag before posting, preserving a toast already shown by the old
adapter. New tag aliases are package-private and app-scoped; retain them while
matching notifications or pending work remain addressable.

Queue S_OK includes an already-pending open; S_FALSE must not create a new
engine event/cache. Lookup is payload inspection, not consumption or proof that
an open is new. Before acknowledging an OPENED event, Flutter durably upserts
the full RemoteMessage and preserves an existing consumed tombstone. Flutter
owns initial-message consumption and never asks the SDK to parse its private
cache. SDK-owned pending opens survive callback failure or process death.

The SDK retains ten fully acknowledged pure-data receipts per Firebase app.
Notifications retain the newest ten receipt IDs after all presentation,
callback, open and metrics work is acknowledged. Lookup of an older collected
receipt returns S_FALSE; a previously consumed launch cannot replay its event.
Business archives and the engine's full initial-message cache remain with their
recipients. Pending work/opens/metrics, unopened notification payloads and
unplanned legacy receipts are preserved. Candidate Runtime52 also collects a
tag alias before presenting a tagged notification when no retained receipt
references its logical tag and no current Windows notification uses its native
tag. It rechecks all receipt references under the state transaction; failure to
read Windows history or perform maintenance does not block presentation.
Aliases with unknown extension metadata are preserved. Runtime51 does not run
this alias collector. Unopened payloads have no count/age eviction policy; the
native state file rejects replacement above 16 MiB without replacing committed
state. OS history absence and FCM server TTL do not establish safe payload
expiration. Candidate52 remains separate from the frozen Flutter51 integration.

The unreleased capacity fix reserves fixed-width receipt lifecycle metadata at
acceptance. It reads legacy boolean flags and migrates a record on its next
lifecycle update without discarding its payload or increasing the 16 MiB quota.
Queueing and acknowledging an open therefore do not need additional state-file
space. Metrics acknowledgement uses the same reserved word. The private record
format changes; quiesce old SDK writers before updating and do not downgrade to
Runtime52 or earlier after the new format has been written. The public ABI and
engine-owned initial-message cache do not change. A full file still rejects new
receipt admission; this fix does not claim that every operation allocating new
state (such as planning an unplanned legacy record) can succeed at capacity.

## Notification resource builds

Notification resource aliases belong to the application's normal MRT resource
build. Include the RESW mappings described in SDK_ABI.md and their packaged
assets when generating the application's PRI. Keep the application's own
resources in that same build; do not replace its resources.pri with a fixture
PRI or a Framework PRI. Its resource map name must match the app identity.

For a single APPX, generate one combined PRI containing every language/scale
candidate. MakePRI's generated packaging section can split qualifiers into
satellite PRIs; those require their own complete resource-package pipeline.
The single-APPX verification sample uses the documented combined-PRI path,
retaining both scale100 and scale200 candidates in one resources.pri.
See Microsoft's [MakePRI configuration documentation](https://learn.microsoft.com/en-us/windows/uwp/app-resources/makepri-exe-configuration).

The native sample can be built from a published consumer artifact:

```sh
python3 tools/build-wpn-resource-consumer.py prepare CONSUMER OUTPUT
```

On a Windows build host, run the sample's resource compiler step using the
Windows SDK MakePRI executable (the fixture's default resource language is
explicitly en-US):

```powershell
./tools/build-wpn-consumer-resources.ps1 -MakePri MAKEPRI_EXE -PackageRoot OUTPUT/receiver -OutputDirectory RESOURCE_BUILD
```

Copy RESOURCE_BUILD/resource-dump.xml to OUTPUT/resource-dump.xml, retain the
resulting OUTPUT/receiver/resources.pri, and package with:

```sh
python3 tools/build-wpn-resource-consumer.py package CONSUMER OUTPUT
```

This sample compiles ordinary host glue and app code against the consumer's
published headers/import library and merges its actual manifest fragments.
No SDK implementation or private DLL is bundled. Signed sample construction
and PRI inspection do not substitute for actual Phone execution or audible
custom-audio verification.

## Verification

Runtime38 passed actual cold FCM data and standard notification delivery with
native background completion and Windows history, and the view-loss/replay,
async context lifetime, real OS deferral/cancellation and cold PRI resource
fixtures. Native FIS component error/commit/delete/recreate checks passed on an
isolated network-capable app. These results do not prove Flutter's published
API, Dart callbacks/isolate completion, warm/cold OS taps or every networking
cancellation/retry path. URI/XML checks do not establish audible custom sound.

The release ledger records exact candidate evidence, cleanup and remaining
gates: `logs/firebase-compat-release-gates-20261016.json`. API details and ownership/lifetime
rules are in [SDK_ABI.md](SDK_ABI.md).
