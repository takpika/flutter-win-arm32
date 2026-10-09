# Windows Phone Firebase consumer SDK

Add `include` to the compiler include path and `lib/arm/wpnotify.lib` to the
ARM32 link. Insert `PackageDependency.xml` inside the app manifest Dependencies.
Merge AppServiceExtension.xml into the app Application/Extensions for
in-process delivery, and retain internetClient capability. The host must route
real CoreApplication.BackgroundActivated to SDK start_background_saved for
RECOVERY/DELIVERY while retaining the SDK deferral. This route requires Windows
10 build 14393 or newer; an out-of-process host must provide its actual entry
point and forward its IBackgroundTask instance instead. Framework dependency
alone cannot add an app's service manifest entry or engine startup hooks.
Install the signed Runtime Framework and hidden Hub from this exact build.
The DLL is supplied by the Framework: never copy it into the application.
Name/Publisher/MinVersion are taken from the signed Framework, not Flutter code.
CN=MacMobileDev is the development publisher for these artifacts.

Read consumer.json for ABI requirements. ABI15 structures keep their exact
layout, including their size field. New optional functions must be resolved
before use; reject a runtime below the consumer requirement. An exact ABI
version equality check would unnecessarily reject additive future versions.
This artifact exposes only its actual header/API. consumer.json advertises
token-refresh (required ABI16), auto-init (required ABI17), delivery metrics
(required ABI18) the Core-default getter (required ABI19) and atomic legacy Core import
(required ABI20) separately. Header presence is not evidence of
completed native integration; consult the validation report for the build.

Windows notification settings are read-only OS authority. No permission dialog
is shown. requested provisional (bit13), Apple options and unknown bits return
in unsupported_requested_fields. Do not translate unsupported to user denial.
SDK authorization values are defined in notification_settings.h and must be
mapped to Dart enums by the Flutter bridge, never cast directly.

Auto-init resolves persisted messaging override, signed package metadata, then
Firebase Core data-collection default (true when absent). The optional signed
`firebase-messaging-defaults.json` has version=1, optional autoInitEnabled and
optional dataCollectionDefaultEnabled booleans. To inherit Core, omit
 autoInitEnabled. The Core bridge should receiver-only open and migrate an
legacy saved Core value with import_data_collection_default_enabled. S_OK
means imported; S_FALSE keeps the existing SDK value; both return that value.
This atomic import can be retried after an engine-marker failure without
overwriting a newer SDK value. Preserve legacy provenance in the engine.
Use set_data_collection_default_enabled for explicit changes. When
no explicit Core value exists, read get_data_collection_default_enabled instead
of writing a bridge-local true default. Then open_with_options. The Core getter
resolves saved explicit Core value, signed dataCollectionDefaultEnabled, then
true, independently of messaging overrides. This avoids overwriting a packaged
false or starting automatic network work before a false Core value is known. Saved/background opens reuse the committed setting. Explicit get/topic
operations remain available while auto-init is false; settings are not Windows
notification permission. Disabling stops future automatic passes; in-flight
work may finish. Delete suppresses implicit recreation for this process until
explicit enable/get or a new process startup. SDK owns scheduling/backoff.

Attach each actual UI view on its UI thread after CoreWindow creation. Restore
[DEFAULT] with open_saved on workers/background; never parse SDK private state.
Keep the callback context/engine module alive until SDK release. Persist the
full cold initial RemoteMessage in the Flutter cache before returning S_OK for
opened; Dart initial-message consumption belongs to Flutter. Transport ACK
follows durable SDK acceptance, independently of analytics/Dart dispatch.

Notification resource names are resolved by the SDK from the app's normal
Windows MRT Resources map. Merge the named RESW entries described in
docs/SDK_ABI.md into the app's resource build, alongside its packaged image/audio
files. Compile the app's combined PRI normally; do not overwrite its existing
resources.pri with a separate notification-only index. No resource lookup or
notification display policy belongs in Dart/engine callbacks.

This is a development consumer artifact; real Hub/Flutter delivery and OS tap
integration remain verification gates. See docs/SDK_ABI.md and docs/MIGRATION.md
for per-candidate native validation limits and migration contracts.
