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
