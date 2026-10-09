# Flutter Windows ARM32

This port targets Windows RT through the Win32 embedder and Windows 10 Mobile
through a UWP host. Both use Flutter 3.38.10
(`c6f67dede3d4aa1aa7a69dd56a3494a5cde6cc80`), Dart
`7c8a3e077e8a3c6209c865301aada26e9fde764a`, and one shared ARM32 snapshot backend.

## Status

This is a source port under validation, not a finished SDK release. Complete
source builds and ordinary LocalSend CLI builds passed for both runtime
families. Their packaged runtime DLLs matched the source build outputs.
Japanese text, font icons and settings input/redraw passed on both devices.
Native frames used NVIDIA Tegra 4 on RT and Qualcomm Adreno 304 on Phone;
neither selected adapter had a software flag.

Final RT HTTPS tests sent 41 bytes and received 44 bytes, with exact payload
readback in both directions. Final Phone tests selected a folder through the
OS picker, enumerated its file and sent 45 bytes with exact peer readback.
Phone receiving saved 46 bytes. A device script required the expected hash
and length before deletion; its subsequent JSON serialization failed, so the
payload was not returned to the host. Independent checks confirmed cleanup.
Phone also opened its own `localsend.org` link through the OS URI API; Edge
loaded the website and the native response completed after returning to
LocalSend. Quick Save settings were restored to off, and temporary files,
processes, test tasks and inbound firewall rules were removed.

The common FML checks and glyph options retain their upstream implementations.
RT atlas rendering needs a one-line ANGLE fix selecting the existing low
feature-level vertex conversion table for 9.1/9.2 as well as 9.3. This preserves
atlas rendering and leaves higher feature levels unchanged.

Portable SDK assembly, relocated application builds and actual GitHub Actions
execution remain pending. Debug/profile distribution artifacts, desktop-only
APIs and mobile photo-manager integration still need final feature checks.
WebRTC signaling closed before HTTP upgrade on both RT and a Mac protocol
probe; the port does not suppress that exception or work around it in app code.

Application Dart code belongs to the application. Platform integration belongs
to the embedder and native OS adapters. MSVC compatibility belongs to the scoped
compiler/toolchain. System DLLs are not included in application packages.

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
an ordinary `ubuntu:24.04` container. The distributed SDK targets Linux x64;
macOS ARM64 remains a local development environment. The build uses an isolated source-built LLVM compiler,
Windows GNU libraries, pinned Windows SDK headers, ATL and C++/WinRT inputs.
The build does not change a shared compiler installation.
On Linux, the Windows compiler drivers expose SDK headers through a private
case-insensitive Clang VFS overlay. This preserves Windows include filename
semantics without renaming shared headers or changing application includes.
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
- `assemble_sdk.py` installs both families into a staging SDK whose host/native
  dependencies are already contained within it. Engine archives alone are not
  a usable Flutter SDK.
- `configure_sdk_dependencies.py` connects those prepared distributions to
  relative native/Cargo settings. Assembly accepts its generated settings
  through `--dependency-config`. Full configuration and SDK relocation
  validation remain pending.

The SDK job now connects the prepared artifacts, prepares Windows headers and
ATL, configures the contained dependencies, assembles both engine families,
and builds unchanged LocalSend after moving the SDK. It then packages the SDK.
This job has passed local syntax checks; actual GitHub execution is pending.
A release must also pass physical validation with the final runtime outputs.
`windows-arm32-release.yml` provides manual release publication using the
successful SDK build run and physical validation record. It verifies the
archive and runtime DLL hashes and preserves existing release tags.
This release workflow has not yet run or published a release.

See [the implementation and validation notes](PORTING.md) for compiler build
commands, dependency preparation, SDK assembly inputs, native adapter details,
AOT conversion, signing, and outstanding validation limits.
