# Flutter Windows ARM32

This port targets Windows RT through the Win32 embedder and Windows 10 Mobile
through a UWP host. Both use Flutter 3.38.10
(`c6f67dede3d4aa1aa7a69dd56a3494a5cde6cc80`), Dart
`7c8a3e077e8a3c6209c865301aada26e9fde764a`, and one shared ARM32 snapshot backend.

## Status

This source port is under validation. A release requires matching RT/Phone
source builds, normal application builds after SDK relocation on every host,
and physical records for the exact distributed runtime bytes. Consult the
[Releases page](https://github.com/takpika/flutter-win-arm32/releases) for each
archive's checks and remaining limits; component build results alone are not
a complete SDK validation.

The required distribution set matches the official **3.38.10** SDK hosts:
Windows x64, Linux x64, macOS x64 and macOS arm64. `sdk-hosts.json` pins their
official archive names, checksums and shared framework revision. The host
identity is independent of the ARM32 Windows target. Every CI build runs in
an ordinary Ubuntu 24.04 amd64 container. Windows tool execution uses Wine;
macOS SDKs additionally require actual macOS execution, with x64/Rosetta
identified separately in their native validation records.

GitHub release assets must each be smaller than 2 GiB. Larger SDK archives use
numbered parts with a checksum manifest. The release notes provide the built-in
`copy /b` (Windows) or `cat` command to reconstruct the original archive. Verify
its `.sha256` before extracting it. This changes only the download transport;
no SDK files or functionality are removed.

The [native macOS SDK validator](testing/macos-sdk-validation.md) runs ordinary
RT/Phone application builds before and after SDK relocation. The publisher
binds that later record to the unchanged cross-prepared archive instead of
pretending Ubuntu executed the macOS tools.

Application repositories and pub-cache sources are preserved. Two older native
sources have separate, checksum-pinned corrections in build-output copies:
the Windows runner's signed/unsigned length comparison, and
`file_selector_windows` 0.9.3+3's two `HRESULT` conversions to `int64_t`
(with `<cstdint>`). These reproduce with MSVC and are not compiler compatibility
fixes. [The CMake adapter](toolchain/native-package-compat.cmake) applies them
only when the original source hash matches; other source revisions are left
alone. MSVC compiler differences are handled by the shared LLVM toolchain.

The Windows SDK uses the official `x86_64-pc-windows-gnu` Rust host
distribution with the same contained LLVM/MinGW toolchain used by the SDK's
native tools. The compiler, Cargo, host standard library, MinGW component and
Rust sources are preserved and audited together. This selects the ABI of tools
running on the build computer; RT and Phone retain their ARM32 MSVC target
specifications. SDK-scoped linker configuration leaves system Rust and compiler
installations unchanged. Native host panic unwinding, destructor execution and
Unicode file I/O passed for both contained Rust versions, followed by complete
RT/Phone application builds.

Physical RT fixtures have exercised Japanese rendering, the native file picker,
hardware rendering on NVIDIA Tegra 4, and outbound/inbound LAN file transfer
with full byte and SHA-256 readback. Validation uses the actual adapter returned
by the Flutter view; a legacy adapter description alone is not a software-GPU
verdict. Test files, processes, tasks and temporary firewall rules are removed.

Phone checks cover startup, touch/SafeArea/system bars, folder selection, URI
launching, transfer, theme/accent changes, Continuum input/display behavior,
and suspend/resume. Completed records must identify their actual runtime bytes
and distinguish normal CLI builds from diagnostic hosts. FCM additionally
requires real Dart delivery and token/topic/settings lifecycle checks; native
registration alone is insufficient.

The UWP host requests hardware D3D11 without capping every phone at feature
level 9.3. The pinned ANGLE backend enumerates 11.1 through 9.1 and selects the
supported level. Actual-device diagnostics record that level and reject
software adapters; the Win32 RT renderer uses its existing initialization.

The isolated physical LocalSend fixture contains only the explicitly approved
two-line application send-session fix. The SDK is packaged before applying it,
and normal application build checks use unchanged source. This application
fix is not an engine or SDK build hook.

The pinned LocalSend development commit's Rust v2 response DTO rejects the
protocol's lowercase device type values. The transfer peers omitted that
nullable field, as permitted by the
[v2 protocol](https://github.com/localsend/protocol/blob/main/README.md).
This test establishes real file transfer through the current runtime; it does
not establish compatibility with peers that supply the rejected value. No
engine workaround or additional application patch was added for that issue.

The common FML checks and glyph options retain their upstream implementations.
RT atlas rendering needs a one-line ANGLE fix selecting the existing low
feature-level vertex conversion table for 9.1/9.2 as well as 9.3. This preserves
atlas rendering and leaves higher feature levels unchanged.

Debug/profile distribution artifacts, desktop-only APIs and mobile
photo-manager integration still need final feature checks.
WebRTC signaling closed before HTTP upgrade on both RT and a Mac protocol
probe; the port does not suppress that exception or work around it in app code.

Application Dart code belongs to the application. Platform integration belongs
to the embedder and native OS adapters. MSVC compatibility belongs to the scoped
compiler/toolchain. System DLLs are not included in application packages.

## Use the packaged SDK

Validated Releases provide the SDK archives, checksums and exact validation
records for all four hosts. Unix hosts use tar archives; Windows uses ZIP.
Actions artifacts are also available for development, with cross-preparation
and native validation kept distinct. Reassemble numbered release parts first
when present. For example, on Linux:

```sh
sha256sum --check flutter-windows-arm32-linux-x64.tar.gz.sha256
tar -xzf flutter-windows-arm32-linux-x64.tar.gz
export PATH="$(pwd)/flutter/bin:$PATH"
```

Then open your application repository and run the commands below. The SDK
contains the prepared compiler, Python, Rust, packager, headers and both runtime
families; rebuilding the engine is not part of an ordinary application build.
Install Git, CMake and Ninja on the host PATH. They are build prerequisites and are
not included in the SDK; the ARM32 path uses the SDK's compiler rather than
Visual Studio's compiler.
Applications retain their own normal build prerequisites. For example,
LocalSend's repository requires its generated MSIX helper before compilation.

## Application builds

With the matching engine cache and SDK toolchain installed, applications use
the regular Flutter build command:

```sh
# Windows RT / Win32
flutter build windows --release --target-platform windows-arm

# Windows 10 Mobile / UWP
flutter build windows --release --target-platform windows-arm --windows-family phone
```

The RT family is the default. Phone produces an Appx package; developer signing
and device deployment are separate from compilation. These commands currently
require the prepared port SDK described below. Debug/profile distribution
artifacts have not been validated.

## Build the port from source

Run commands from the Flutter repository root. CI uses Ubuntu 24.04 amd64 in
an ordinary `ubuntu:24.04` container. The release host matrix is listed above;
macOS ARM64 is also a local development environment. The build uses an isolated source-built LLVM compiler,
Windows GNU libraries, pinned Windows SDK headers, ATL and C++/WinRT inputs.
The build does not change a shared compiler installation.
On Linux, the Windows compiler drivers expose SDK headers through a private
case-insensitive Clang VFS overlay. This preserves Windows include filename
semantics without renaming shared headers or changing application includes.
The linker uses COFF's existing VFS resolver for SDK import-library names,
including the original source's `#pragma comment(lib, ...)` directives.
The checkout preparation also reads ANGLE's pinned ASTC dependency from its
own DEPS, fetches that source and checks every ASTC build input. Flutter's root
DEPS does not recurse into ANGLE. Existing modified dependency checkouts are
rejected rather than overwritten; ASTC support remains enabled.
When preparing Windows headers, pass `--mingw-sysroot` to
`toolchain/prepare_windows_sdk.py`. Its private overlay corrects MinGW's
[Boolean/Byte boxing template collision](https://github.com/mstorsjo/llvm-mingw/issues/489)
without removing either interface or changing the shared distribution. The C
declarations remain unchanged; the C++ Boolean box uses `bool` and keeps its
original UUID. CI checks both interfaces and their one-byte scalar ABI.

```sh
python3 dev/windows_arm32/prepare_engine_checkout.py --windows-inputs
python3 dev/windows_arm32/prepare_sources.py --verify-only

python3 dev/windows_arm32/build_engine.py \
  --platform both --compiler-platform \
  --output /path/to/build \
  --toolchain-config /path/to/toolchain.json --jobs 2

python3 dev/windows_arm32/build_frontend.py \
  --dart engine/src/flutter/prebuilts/linux-x64/dart-sdk/bin/dart \
  --output /path/to/frontend
```

The engine build produces both runtime families, the matching `gen_snapshot`,
and Flutter platform dill files from source. The snapshot backend generates
A32 instructions, converts identified instruction ranges to fixed-slot Thumb-2,
and checks bytes outside those ranges. It does not reuse an older VM binary.
`--host-macos-sdk` optionally selects an installed host SDK for this build only.

Toolchain configuration changes appear in Ninja's commands, so changing the
selected configuration invalidates existing native compilation commands.
Dependency patches verify their upstream revisions and file hashes before
application; modified sources are not overwritten.

## CI and distribution

- The source-engine workflow supports manual dispatch and builds on changes
  pushed to `windows-arm32-3.38.10`.
- `windows-arm32-compiler.yml` builds and checks the pinned LLVM compiler tools.
- Compiler and engine archives are cached separately by source files, pinned
  dependencies, native job configuration, and installed host package versions.
  Exact input records, archive checksums, and source-build provenance are
  checked on every cache hit. Compiler ABI and linker checks still run; SDK
  assembly and relocated application builds always run. An SDK packaging
  change therefore reuses identical native outputs, while a native source or
  toolchain change rebuilds the affected archives. The first run populates
  these caches from source; historical RT runtime binaries are not used.
- `windows-arm32-engine.yml` builds RT and Phone from the same revision, then
  verifies and combines their engine archives. Its separate C++/WinRT job
  builds the fixed upstream generator and packages the full projection with
  source records and licenses for SDK assembly.
- Its MSIX job builds the pinned Linux packager with bundle support and XML
  schema validation, then checks relocation before packaging its tools and
  licenses. These host tools are separate from application packages.
- Its Python job packages checksum-pinned CPython and Pillow with dependency
  licenses. It verifies the complete file inventory before and after running
  the interpreter from a relocated directory.
- Its Rust job installs fixed compiler versions into a private workspace,
  copies complete Rustc/Cargo/host-library/source components, and checks an
  offline Cargo build after relocation before packaging them.
- Its native Rust dependency job prepares the checksum-pinned Windows ABI
  declarations and Windows Thumb-2 crypto sources used by both RT and Phone.
  It packages complete sources and licenses with relative Cargo patch paths.
- `prepare_uwp_headers.py` installs the matching ANGLE and RapidJSON development
  headers and licenses from the pinned DEPS revisions. This includes the
  verified Phone display interface used by the UWP host; no runtime or system
  libraries are copied from these source checkouts.
- `assemble_sdk.py` installs both families into a staging SDK whose host/native
  dependencies are already contained within it. Engine archives alone are not
  a usable Flutter SDK.
- `configure_sdk_dependencies.py` connects those prepared distributions to
  relative native/Cargo settings. Assembly accepts its generated settings
  through `--dependency-config`. CI validates the contained configuration
  through ordinary application builds after relocating the SDK.
  Both preparation and assembly accept `--host` for the destination SDK;
  assembly also reads its host from the dependency configuration. Conflicting
  host/CPU settings and binaries for another host are rejected. This selection
  does not replace native execution and relocation validation of that SDK.

The engine and compiler workflows accept an optional `compiler_run` input to
deploy the common LLVM distribution from a successful run of this repository.
The producer sources, patches and build commands must match the current tree;
the archive checksum, complete file inventory and source provenance are checked.
All current language, Windows-header, COM ABI and linker checks still run.
This deployment retains its original build provenance and does not claim the
compiler was rebuilt with the current Ubuntu packages. Exact build-cache
validation keeps its separate, stricter host-input check. Leaving the input
empty uses that cache or builds the compiler from source. This input never
substitutes a previous Dart VM or engine.

For macOS cross assembly, `prepare_rust_distribution.py --reference-rustc`
requires an executing compiler from the identical Rustup channel and commit.
It retains every destination component and audits Mach-O imports without
executing the foreign compiler. `configure_sdk_dependencies.py` accepts
`--reference-rust-stable` and `--reference-rust-193` for this preparation;
`assemble_sdk.py --cross-prepare` verifies the pinned Python/Pillow files.
These records explicitly defer destination execution. The Ubuntu-only
`windows-arm32-macos-sdk-dependencies.yml` workflow prepares both macOS Rust
and Python distributions; those artifacts alone do not establish a validated macOS SDK.
`windows-arm32-macos-tools.yml` separately builds the complete native compiler
(`compile`), ARM32 AOT generator (`snapshot_generator`), or full MSIX packager
(`packager`) for both macOS hosts in Ubuntu amd64 containers. Select one build
per run. The packager retains bundle support, Xerces schema validation, crypto
and compression; Mach-O audits are followed by separate macOS execution checks.
`windows-arm32-macos-sdk.yml` combines successful component runs with the exact
official host cache and the common RT/Phone runtime outputs. It validates the
source manifests and stages complete SDK archives for native execution and
relocation checks; cross preparation alone never marks them release ready.

The SDK job now connects the prepared artifacts, prepares Windows headers and
ATL, configures the contained dependencies, assembles both engine families,
and builds unchanged LocalSend after moving the SDK. It then packages the SDK.
The complete job has passed on GitHub Actions for both runtime families.
A release must also pass physical validation with the final runtime outputs.
`windows-arm32-release.yml` provides manual release publication using the
successful SDK build run and physical validation record. It verifies the
archive and runtime DLL hashes and preserves existing release tags.
Publication requires this verification to succeed; CI artifacts alone do not
establish physical validation.

The UWP host adapts CoreWindow accelerator events to the upstream Windows
keyboard handlers and key maps. CoreText retains responsibility for text and IME
composition; the Win32 runner continues to obtain key state through Win32.

Phone validation must also cover SafeArea and system bars, OS theme and accent
updates, Continuum capture/hover/scroll/secondary-button/resize behavior,
keyboard navigation and text input, and suspend/resume. FCM validation requires actual Flutter foreground, background,
cold-start, initial-message and notification-tap delivery, token lifecycle,
topics, notification settings and legacy-state migration. Native SDK registration
or compilation alone does not satisfy those checks.

The Firebase adapter delegates registration, durable inbox state, token/topic
operations and Windows notification integration to the separately provided
native SDK. Flutter owns engine/isolate startup, method-channel replies and
awaiting the application's actual background Future. The consumer's development
and integration-release metadata remain explicit; a successful Flutter fixture
does not promote the native SDK's separate release status.

Both runtime families request Flutter's existing Windows
`angle_force_context_check_every_call` setting. Upstream's build-host OS check
otherwise omits it when cross-building on Linux or macOS. See
[Flutter's ANGLE investigation](https://github.com/flutter/flutter/issues/114107).
Physical records must exercise the rebuilt runtime; a source setting alone is
not evidence of a resolved GPU failure. Background execution also retains the
real Windows task budget and cancellation behavior. See Microsoft's
[cancellation reasons](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.background.backgroundtaskcancellationreason)
and [background task guidelines](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/guidelines-for-background-tasks).

Before publishing the complete four-host set, `verify_sdk_set.py` accepts a
JSON manifest with `schemaVersion: 1` and one `hosts` entry per `sdk-hosts.json`
identity. Each entry supplies `host`, `archive`, `validationRecord`,
`buildRunId` and `sourceRevision`; file paths are relative to the manifest.
It runs the physical release verifier on every archive, checks its bootstrap
host/revision and native Dart/AOT generator binaries, and requires identical
RT/Phone runtime bytes and one source revision across all four hosts. An
incomplete set is rejected. This check does not itself upload or publish assets.
`windows-arm32-multi-host-release.yml` connects that check to manual publication.
Its `sdk_records` input contains `schemaVersion: 1` and the four `hosts` entries
with `host`, `buildRunId`, `sourceRevision` and an inline `validation` record.
It accepts only successful push/manual SDK workflows from this repository
whose exact commit is now published on `windows-arm32-3.38.10`. Promoting the
same validated commit does not require rebuilding just to change a branch label;
an older commit is rejected even if its run used the public branch name.
It verifies archive checksums and the complete set, then publishes
all archives, checksums and evidence under a new tag. Existing tags are retained.

See [the implementation and validation notes](PORTING.md) for compiler build
commands, dependency preparation, SDK assembly inputs, native adapter details,
AOT conversion, signing, and outstanding validation limits.
