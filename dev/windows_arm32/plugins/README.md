# Native plugin adapters

UWP-specific implementations of package platform channels belong to native
plugin adapters, separately from the Flutter host's rendering, input, metrics,
text-input and lifecycle services. Application Dart and generated Pigeon
contracts stay with their packages.

`file_selector_windows/file_selector.h` implements the package's existing
Pigeon contract using real Windows.Storage.Pickers operations and the SDK's
brokered-storage authorization. Supply the original package's Windows directory
on the include path for `messages.g.h` and compile its matching `messages.g.cpp`.
The adapter has no application repository path or LocalSend-specific include.
The picker function bodies were retained from the physically tested Phone adapter.
The adapter uses the SDK's namespaced `runtime_support.h` COM and asynchronous
primitives. Standalone compilation no longer includes the diagnostic app,
its allocator override, storage probes or hard-coded runtime class name.
An actual dispatch entry point compiles against the current compiler, SDK and
original package contract, emitting native picker activation and codec calls.
Generic-host registration, linking and package generation have passed.
An application built with the normal Flutter CLI and the original Dart package
selected a folder on the Mobile test device, enumerated and read its files, and
wrote, read and deleted its own output. Missing-file, directory and read-only
file deletion checks also passed. This does not establish every file-picker or
save-dialog contract.

`url_launcher_windows/url_launcher.h` retains the native Windows.System.Launcher
URI launch and support-query implementation. `window_manager/window_manager.h`
retains the window_manager and screen_retriever_windows implementations; visible bounds
are read directly from the OS using the extracted ApplicationView helper.
Neither header includes a diagnostic app or an application repository path.

Each adapter has a separate `registration.h`. Generated SDK build glue should
include and register only adapters whose packages are in the dependency graph.
The host's `PlatformMessageHandlers` registry contains generic callbacks and
host context; it has no package channel names, codecs or implementation imports.
Registered callbacks own their plugin instances. The host completes an
unhandled message after both package dispatch and Flutter core services decline
it, using the standard unimplemented response.

The host uses `FlutterEngineInitialize` to obtain an engine handle, runs the
native initializers, then calls `FlutterEngineRunInitialized` to start Dart.
This gives initializers an engine handle while preventing Dart from accessing
their native state before initialization. The two-stage startup, hardware
Flutter presentation and an original folder-picker channel request passed on
the Mobile test device. Plugin-specific executable FFI integration still needs
its own validation.

The registrations and actual dispatch paths compile and link together for
ARM32 UWP with warnings treated as errors. Generated build glue uses the
native package name `screen_retriever_windows`, and shares one registration
with `window_manager` when both are present. This verifies native compilation
and dependency separation. Full application plugin coverage and physical
validation of the extracted adapters remain pending.

`dynamic_color/registration.h` implements the original Windows method-channel
contract for `getAccentColor`, using the real
[UISettings.GetColorValue](https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.uisettings.getcolorvalue)
API with `UIColorType.Accent`. Windows exposes that API through the Universal
API contract from Windows 10 build 10240. The adapter returns the package's
unsigned ARGB value in an int64 StandardMethodCodec envelope. It adds no
desktop DWM dependency and changes no package Dart or engine source. Other
methods keep the original Windows package's unimplemented response. OS-query
failures are returned as platform errors. The adapter has compiled and linked
with the other native adapters. In a signed UWP test package on the FTJ152E,
its color query succeeded and the original StandardMethodCodec response decoded
to the same unsigned ARGB value as a direct OS query. This checks the native
adapter and codec, using a test response capture rather than a Flutter engine.

Light/dark preference is separate from the accent-color plugin. The UWP
embedder reads `UIColorType.Foreground` and sends `platformBrightness` through
Flutter's standard `flutter/settings` channel at startup. It subscribes to
`UISettings.ColorValuesChanged`, queues updates onto the platform thread, and
reads the preference again on resume. The application chooses whether to follow
that preference through its normal `ThemeMode`; the embedder does not override
an application's explicit light or dark selection.

The accent adapter answers each `getAccentColor` request with the current OS
value. It does not add an event channel absent from the Windows package contract.
The tested LocalSend revision reads this value during initialization and uses it
when its system color mode is selected. Updating that application's palette
while it remains open depends on the application requesting the color again.
The Mobile host sends the complete settings message, including the OS text
scale and time-format preference required by Dart's decoder. A native host
update over the CI-built package followed OS dark/light/dark changes on the
FTJ152E without restarting the application, and the original OS preference was
restored. Live accent changes and the final CI rebuild remain to be verified.

The Firebase Messaging notification-settings adapter uses the shared Firebase
compatibility Framework's public C ABI. Its header/import-only consumer bundle
provides the manifest dependency; the Framework DLL is installed separately.
The production native getter/request conversion and StandardMessageCodec
round trip passed on Windows 10 Mobile build 14393 with runtime ABI16 using
the ABI15 consumer. Unsupported permission options remain unsupported, and
the read-only request does not show an OS prompt or change permission state.
This verifies the native boundary; Flutter/Dart message delivery, background
execution, token lifecycle and migration remain integration gates.

`connectivity_plus/registration.h` preserves the original method and event
channel names and StandardMethodCodec list responses. It implements `check`,
`listen` with an initial status, and `cancel` using the real
[NetworkInformation APIs](https://learn.microsoft.com/en-us/uwp/api/windows.networking.connectivity.networkinformation).
It enumerates current profiles and includes local connectivity without requiring
internet access. Native interface types distinguish Ethernet, Wi-Fi and VPN;
the OS WWAN profile flag identifies mobile data. No connected profile produces
`none`, while OS-query failures produce an error envelope.

OS network notifications schedule work on the CoreDispatcher. Each queued task
keeps a weak reference to its subscription and checks its active flag; cancel
and destruction deactivate that flag before unregistering the OS callback.
The generic host releases its plugin registrations before shutting down the
engine. Channel names, network queries and event implementation remain in the
plugin adapter. Compilation and linking with the other adapters have passed
with warnings treated as errors. The native `check` method passed in a signed UWP test package on the FTJ152E,
returning `wifi` and matching a direct OS query through the original codec.
Cancellation without a listener also passed. A subsequent UWP test using the
real CoreDispatcher and OS subscription passed initial status delivery,
cancellation of an active subscription, suppression of queued initial events
after cancel, and suppression after clearing host registrations. The event
transport used a native test capture callback and the original codec. Actual
OS events caused by a network-state transition, and full Flutter application
event delivery, remain to be verified. Cancellation before listen
or after a failed listen is idempotent and does not activate the OS service.
Allocation failures while creating a subscription return an HRESULT error
instead of escaping into the platform-message callback.

`uri_content/registration.h` preserves the original Windows package channel and
its sole implemented native method, `getPlatformVersion`. It queries
`AnalyticsInfo.VersionInfo.DeviceFamilyVersion` through the real WinRT ABI and
returns the original Windows version-label format through StandardMethodCodec.
Other methods retain the original package's unimplemented response. The ABI
interface IDs and method order match the Microsoft projection. The adapter
compiled and linked with all six existing registrations. Native physical
query and dispatch verification remain pending; no application source changed.

`open_dir_windows/registration.h` keeps the original `openNativeDir` method,
channel, path and highlighted-file arguments and boolean success response.
It uses StorageFolder and Launcher.LaunchFolderAsync; FolderLauncherOptions
selects the requested item. C++/WinRT preserves the calling UI apartment
across awaits. A weak registration lifetime prevents subsequent launches and
responses after the host clears its handlers. OS failures return false as in
the original ShellExecute backend. Native host compilation/linking passed;
actual Phone launch, selection and cancellation remain unverified.

`pasteboard/registration.h` preserves all five implemented Windows methods
(`image`, `files`, `html`, `writeFiles`, `writeImage`) through the original
StandardMethodCodec contract. Clipboard/DataPackage provide storage items, HTML
and bitmap data. Images are decoded to opaque BGRA8 and encoded as BMP, matching
the original Windows XRGB bitmap path. Image reads return an owned temporary
file path that the unchanged Dart implementation reads and deletes. Image
writes materialize their bytes in a retained in-memory stream before the
unchanged Dart caller deletes its source. Clipboard.Flush persists writes.
Registration lifetime guards suppress writes and replies after host teardown,
and failed/cancelled image-read outputs are deleted. OS errors use the original
error code `0`. Combined native compilation/linking passed; actual clipboard
format round trips, image bytes, permissions and cancellation remain pending.

`gal/registration.h` bridges all six original Windows gallery methods. The SDK
build extracts the checksum-pinned package's existing WinRT helpers verbatim
into its generated build directory, excluding only the desktop registrar glue.
The package source is not edited. Both image and video paths still use Pictures,
and byte uploads retain the original format signatures and collision policy.
UWP packaging declares `uap:Capability` for picturesLibrary when this package is
selected; the package's access methods query the real broker rather than
returning the desktop backend's unconditional true. The adapter awaits the
original helpers on the UI apartment, preserves the original storage error
codes, and suppresses replies after registration teardown. Compilation and
linking with the existing adapters passed. A signed native test package on the
FTJ152E passed both access queries, image-byte storage and byte equality, duplicate
name handling, image/video path copies to Pictures, and the original unsupported
format error response. The path-copy test used the same BMP input for both media
methods; it does not verify video playback. All four copied files matched the
source bytes. The owned album, test package and staging file were removed. This
uses the real registry and StandardMethodCodec with a native response capture,
not a Flutter engine. A separate signed package without picturesLibrary also
passed: the OS returned E_ACCESSDENIED, both access methods returned false, and
image-byte storage returned the original UNEXPECTED error instead of success.
It created no media, and its package and staging file were removed. Photos
activation and full Flutter round trips remain to be verified.

`desktop_drop/registration.h` uses CoreDragDropManager and an
ICoreDropOperationTarget to deliver the original entered, updated, exited and
performOperation MethodChannel events. It accepts storage-item copy operations,
retains brokered access to files and folders in the SDK, and sends their original
paths. The host supplies a generic initialization callback after creating the
engine; the adapter owns its OS subscription and revokes it before engine
shutdown. Outstanding targets hold weak registration state, and queued async
operations check an active flag on the captured UI apartment before sending.
Coordinate conversion follows WinUI's forwarding of CoreDragInfo.Position to
XAML root hit testing and the unchanged package's Windows DPI division; actual
OS drag coordinates still require physical verification. The combined native
host compiled and linked with warnings treated as errors. A signed native
probe on the FTJ152E passed the actual production initializer, rejected duplicate
initialization, and revoked its subscription on Clear. Synthetic Enter/Over/
Leave/Drop calls after Clear sent no messages, including when an outstanding
operation kept state alive and when state had expired. The owned probe package
and staging file were removed. Actual OS file drops, brokered file reads, drag
coordinates and Flutter event delivery remain unverified.

Firebase integration is under development. The checksum-pinned shared consumer
contains headers and an ARM import library; its separately installed Framework
supplies the DLL. The adapter requires ABI22. The SDK owns installations, tokens,
topic policy/retries, delivery metrics, notification presentation, durable SDK
receipt stages and real OS background-task scope. Flutter does not embed another
transport implementation or modify these SDK-owned state modules.

`firebase_messaging/sdk_client.h` binds the real Windows Core identity separately
from the Android FIS identity. It opens a receiver, restores the SDK Core default
or atomically imports legacy preferences, and only then publishes network options.
Fresh explicit changes use the SDK setter. `sdk_execution.h` retains callback
custody through SDK release, including cancellation and engine shutdown. Real
BackgroundActivated task instances are passed synchronously to the SDK; the
adapter creates neither another OS deferral nor its own transport worker.

Flutter retains its channel protocol, Dart callback registration, token-event
custody and getInitialMessage consumption. Real OS toast arguments remain opaque.
The SDK's ordinary message callback includes background data. Flutter queries
the SDK's real OS foreground state to select `onMessage` or the registered
background callback; an unavailable state leaves the SDK receipt unacknowledged.
Before Dart/Core initialization, the native worker restores an existing saved
SDK client and queues the real OS activation, protecting its SDK receipt. It
retains the durable raw intent until Core identity is available for full initial
message custody; it creates no Core app or guessed SDK descriptor.
Only SDK queue acceptance may create a new initial message; complete public
message data is committed before SDK opened acknowledgement. Process-death replay
cannot revive consumed receipts. SDK FCM receive activation uses the default app,
matching the original firebase_messaging package's public API. Firebase Core
secondary apps retain their independent Core identities and preferences.

The native adapter subscribes to real CoreWindow visibility and application
resume events. It posts through the UI dispatcher before waking its plugin
worker, allowing the SDK's OS observers to finish first. A wake arriving during
an unfinished drain is retained and coalesced into one subsequent drain. Pending
or canceled results alone never cause an immediate retry loop. Headless engines
do not install these subscriptions or consume foreground-only message custody.

The original public Firebase Core and Messaging Dart API contract can be tested
from `firebase_messaging/tests/dart` with `flutter pub get` followed by
`flutter test`. These tests use the real public packages and codecs with a mocked
native channel; they cover callback replies and API mapping, not physical FCM
delivery or OS task lifetime.

The ownership, native RPC, cancellation, token-generation and notification-custody
tests pass under address/undefined-behavior sanitizers and ARM Windows headers.
The complete shared-SDK Flutter transport handoff, real cold notification task
lifetime and physical Dart delivery remain integration gates. These tests do not
establish a completed FCM integration release.
