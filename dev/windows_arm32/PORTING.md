# Windows ARM32 port

The source base is Flutter 3.38.10 (`c6f67dede3d4aa1aa7a69dd56a3494a5cde6cc80`)
with Dart `7c8a3e077e8a3c6209c865301aada26e9fde764a`. Windows RT uses
the Win32 embedder; Windows 10 Mobile uses a UWP host. Both targets use the
same patched Dart sources and the same snapshot backend.

## Current CI host

All build and release jobs use Ubuntu 24.04 amd64 with an ordinary
`ubuntu:24.04` job container. Host executables and the public SDK target Linux
x64; Windows target headers, libraries and ARM32 runtime families are unchanged.
ELF dependencies and relative runtime search paths are audited without copying
OS libraries. macOS measurements elsewhere in this document are local
development evidence, rather than Linux CI validation.

The UWP pointer path distinguishes mouse, touch and stylus devices, maintains
state per pointer, forwards mouse button masks and hover/scroll events, and
uses CoreWindow for system cursor changes. These are embedder responsibilities;
the Flutter application is unchanged.

## Source preparation

After fetching the engine's pinned dependencies, prepare its Dart checkout:

```sh
python3 dev/windows_arm32/prepare_dart.py engine/src/flutter/third_party/dart
python3 dev/windows_arm32/prepare_dart.py engine/src/flutter/third_party/dart --verify-only
python3 dev/windows_arm32/prepare_angle.py engine/src/flutter/third_party/angle
python3 dev/windows_arm32/prepare_angle.py engine/src/flutter/third_party/angle --verify-only
python3 dev/windows_arm32/prepare_skia.py engine/src/flutter/third_party/skia
python3 dev/windows_arm32/prepare_skia.py engine/src/flutter/third_party/skia --verify-only
```

The preparation commands check the upstream revision and every affected file
before applying each patch. They refuse to overwrite modified source files and
verify the resulting hashes. Repeating them on a prepared checkout is safe.
The ANGLE patch retains upstream SwapChainPanel source selection and detection.
The Skia patch selects UWP OS entry points; MinGW DirectWrite declaration
compatibility lives in the scoped SDK headers, keeping Skia's COM calls unchanged.

To prepare all native dependencies together:

```sh
python3 dev/windows_arm32/prepare_sources.py
python3 dev/windows_arm32/prepare_sources.py --verify-only
```

This also covers ICU, BoringSSL, Abseil, Perfetto and SwiftShader. Each dependency has a pinned
revision and per-file hashes. Perfetto's original child-process implementation
is retained; its OS behavior remains to be validated. SwiftShader retains its
backends and diagnostics. Its older Windows support receives ARM32 stack-context
fields and platform-counter timing; the GNU driver supplies the CRT configuration
for its older LLVM headers. Other architecture branches stay unchanged.

## Responsibilities

- Flutter tools select Windows ARM32 artifacts and invoke the normal application
  build. Application Dart code and generated Windows project files remain owned
  by the application.
- Engine and platform hosts implement rendering, input, window metrics and OS
  integration. RT keeps the Win32 host; Phone's UWP-specific behavior stays in its
  platform boundary.
- Dart supplies the Windows ARM32 ABI and VM support. The snapshot backend in
  `aot/` generates A32 code, converts identified instruction ranges to fixed-slot
  Thumb-2, and checks that bytes outside those ranges remain unchanged. It is
  not a native Thumb-2 emitter in `gen_snapshot`.
- The compiler compatibility patch in `toolchain/` handles the MSVC inherited
  constructor behavior required by unchanged Flutter C++ value types. It is
  restricted to MSVC compatibility mode. The standard language mode and ordinary
  member using-declarations retain their behavior.
- The Windows MSVC-mode integer-range patch retains the operand range through
  value-preserving promotions inside explicit no-op cast wrappers. Approximate
  shift diagnostics also follow MSVC's operand-range behavior; exact range
  queries retain their original behavior. This handles upstream ANGLE averages
  and bit masks without changing ANGLE or dropping `/we4244`. C++17/20 checks
  on ARM32 and x64 retain errors for unsafe narrowing and sign-changing casts,
  and retain GNU-mode diagnostics. The original failing ANGLE compilations and
  existing compiler checks passed with an isolated patched compiler. The full
  engine source builds with that compiler passed for both RT and Phone.
- The usual-arithmetic-conversion patch carries arithmetic context to integer/
  floating precision diagnostics in Windows MSVC mode. It retains direct
  assignment/return diagnostics and GNU behavior. The original ANGLE timer
  compilation and C++17/20, ARM32/x64 and existing SEH checks passed with an
  isolated compiler. Small integer member initializations rejected by MSVC
  itself remain upstream source issues; they are not hidden by this patch.
  Arithmetic, compound assignment, conditional and floating comparison operands carry this
  context, including nested conditionals. Returning an integer-only conditional
  expression as a floating type still produces the conversion error. The
  comparison behavior matches MSVC 19.44 with `/W4 /we4244`; the original ANGLE
  D3D11 clamp expression compiles without edits. C++17/20, ARM32/x64, GNU/MSVC
  mode checks and preparation on pristine pinned LLVM sources passed locally.
- Integer bitfield stores follow MSVC's storage-conversion diagnostic behavior
  in Windows MSVC mode. Constant bitfield overflow, floating stores and nested
  argument conversions retain their diagnostics; GNU mode is unchanged.
- In Windows MSVC mode, constant floating-point truncation follows MSVC's
  C4305 warning classification instead of being promoted by `/we4244`.
  The original diagnostic's default enablement is retained. When enabled,
  it remains visible as `literal-conversion`, and global `-Werror` still
  promotes it. Direct variable conversion errors and GNU diagnostics remain
  intact. The original ANGLE YUV constant table compiled without source edits;
  the warning/severity matrix and existing compiler checks passed locally.

To check a source-built compiler against the compiler and Flutter fixtures:

```sh
python3 dev/windows_arm32/toolchain/check_compiler.py /path/to/clang \
  --sysroot /path/to/llvm-mingw \
  --engine engine/src/flutter
```

The compiler also supplies Windows ARM32 SEH filters, parent-frame recovery and
cleanup funclet returns, including aligned and dynamically sized frames.
The fixture checks cover MSVC and GNU targets at O0/O1/O2, plus GNU C++ cleanup
with asynchronous exception scopes. The Windows GNU ABI adapter maps the MSVC
operator-delete linker pin to its GNU symbol and retains `init_seg` ordering.

The LLD patch keeps forwarded-export string addresses separate from ARM Thumb
code addresses. Its one-line correction leaves direct function/data exports and
other architectures unchanged. A regression reproduced the unpatched ARMNT
forwarder-name corruption, while x86, x64 and ARM64 passed. The compiler workflow
checks all four with `toolchain/check_linker.py`. Locally, the patched source-built
linker passed all four architecture checks and the existing ARMNT code/data test.
On the Mobile test device, all nine User32 facade exports resolved to the same addresses as
the real OS MinUser functions, and querying the app's CoreWindow rectangle worked.
These checks do not establish full application or full compiler-distribution
verification.

The Mobile User32 facade also forwards `GetWindowLongPtrW` to MinUser. Mobile
lacks `IsZoomed` in the tested modules and documented API set, so the SDK reads
the actual window's `WS_MAXIMIZE` style for that query. A native device check
verified all ten forwarded function pointers and the state query against the
OS window style. It did not exercise a maximize operation or establish the
bitsdojo Dart integration.

The original Windows `ffi` allocator opens `ole32.dll`. That bare module is
unavailable on the tested Mobile OS, so the SDK redirects that legacy name to
its facade and forwards `CoTaskMemAlloc` and `CoTaskMemFree` to the real OS
`combase` functions. A normal Flutter CLI build using unchanged `ffi` 2.1.4
passed zero-filled calloc, malloc write/read, Japanese and surrogate-pair UTF-16
round trips, matching release calls and hardware Flutter presentation on-device.
No OS DLL was packaged. This covers the allocator path, not the full bitsdojo
native backend.

The CoreWindow host builder now links the original bitsdojo 0.1.6 API table
source, with its five ABI headers/sources verified before compilation. The
Phone native backend supplies its seven public functions and drag channel;
window initialization precedes Dart execution, and host teardown restores the
original window procedure. The combined ARM32 App-partition host builds and
exports `bitsdojo_window_api` and `bitsdojo_window_configure`. This is a build
result, not an on-device or full-plugin completion claim. Custom-frame hit
testing still needs implementation, and original Dart startup, window behavior
and teardown need integration verification.

The `windows_taskbar` native backend now routes all twelve original methods and
thumbnail-button notifications through the shared host session. Its original
UTF-16 utility source is compiled unchanged. COM activation is checked before
using the taskbar interface; title save/restore uses the actual HWND independently
of taskbar activation. Optional shell exports are resolved when needed, and
missing modules/procedures or failed COM calls retain the original method-error
format with an HRESULT detail. The combined bitsdojo/taskbar host compiles and
links. This does not establish taskbar availability or end-to-end application
behavior on Mobile; physical integration verification remains required.

The `tray_manager` adapter implements the six original methods, nested menus,
checkbox/disabled states, icon mouse notifications and menu-item callbacks.
Notification-area APIs are resolved from the OS when used; icon/tooltip state
changes are committed only after successful native calls. Popup tracking holds
the menu alive across reentrant updates, and icons/submenus are owned until
replacement or cleanup. The combined bitsdojo/taskbar/tray host compiles and
links, with no source changes to the cached packages. This is not a claim that
Mobile supplies a desktop notification area or that the complete LocalSend
application has passed physical integration verification.

`plugins/native_window_session.h` owns the host's CoreWindow subclass and
dispatches an immutable list of package-owned native handlers. The generated
registrant creates one shared session before initializing native packages.
Bitsdojo's window policies and FFI payload handling remain in its adapter;
the shared session contains no package channel names or window policies.
Teardown restores the original OS procedure before releasing its module. If an
external subclass prevents restoration, retained handlers see a retired context
with no engine or host pointers; bitsdojo can release queued allocations without
calling the retired engine. The updated SDK host builds, but this shared dispatch
and teardown integration still requires on-device verification.

The `window_manager` adapter uses that same session for its original `WM_CLOSE`
event: it sends `onEvent` with `eventName: close`, and consumes the native close
request when `setPreventClose` is enabled. `isPreventClose` returns the stored
policy, initially false. This does not intercept UWP suspension or OS process
termination. The combined LocalSend native host builds with this handler;
physical close-event cancellation remains to be verified.

`toolchain/compiler-source-manifest.json` records the LLVM revision and patch
hashes. The conversion diagnostic changes are one patch over two source files,
with no overlapping patch entries. Preparing all patches twice against a
pristine pinned source fixture passed; shared compiler sources were preserved.
LLVM's upstream license accompanies the patches.

The compiler Actions job packages all required source-built compiler/LLVM tools
through `toolchain/package_compiler.py`. It preserves executable aliases as
relative symlinks, includes Clang resource headers and licenses, and audits
macOS native dependencies without copying system libraries. The staged libLTO
copy receives a relative install name and an ad-hoc signature; shared build
outputs are untouched. The archive includes payload hashes and source-build
provenance. Missing tools or a compiler/provenance mismatch fail packaging.
Actions then extracts the archive and runs the language, ARM32 exception and
COFF export checks against the extracted tools. These steps are configured;
the complete compiler archive and Actions run still require verification.

Build the compiler tools from a checkout at that revision:

```sh
python3 dev/windows_arm32/toolchain/build_compiler.py /path/to/llvm-project \
  --output /path/to/compiler-build --jobs 2
```

The compiler build includes all LLVM targets and requires zlib, zstd and
libxml2 support. Use `--dependency-prefix` for dependencies outside the default
search paths. Zstd links statically, with its licenses included in distribution.
This preserves the compiler's compression and XML features without changing
an existing shared compiler installation.
The build also produces `libLTO` and the native LLD driver names, so host builds
can use LTO with the matching compiler. The compiler archive contains that LTO
library, `llvm-symbolizer`, and `llvm-dlltool`; LLVM component libraries and zstd link statically.

The compiler workflow at `.github/workflows/windows-arm32-compiler.yml` performs
that source build and packages the native tools with their license and source
provenance. Its GitHub execution remains to be verified. The Phone OS adapter is
a GN shared-library target in
`engine/src/flutter/shell/platform/windows/uwp/compat`; it does not require a
checked-in import-library binary.

Prepare the Phone ANGLE headers from the pinned Microsoft Windows SDK package:

```sh
python3 dev/windows_arm32/toolchain/prepare_windows_sdk.py --output /path/to/windows-sdk
```

An existing package may be supplied with `--sdk-package`. The command checks its
SHA256, retains unmodified Microsoft WRL headers, and generates complete XAML
interface declarations checked against the package's SDK vtables. The local
GNU compatibility headers forward to real Windows APIs. It extracts no system
DLLs or import libraries. SDK provenance and the C++/WinRT license accompany the
headers; the SDK package license URL is recorded in that provenance.
The preparation also supplies the complete XPS thumbnail COM declaration from
the same package. It retains C and C++ vtables and the original CLSID/IID;
Skia's XPS code stays unchanged. This verifies declarations and compilation,
not the availability of the COM server on a particular device.
The SDK headers also retain SAL annotations missing in MinGW, provide the SDK's
UI Automation enums and VariantCompare declaration, and bridge VariantCopy's
const source declaration to its unchanged native ABI. Direct WRL `implements.h`
includes use the same compiler compatibility boundary as the root WRL header.
The iterator adapter honors the upstream MSVC deprecation-silencing macro for
that declaration only.
The SRW-lock adapter completes MinGW's native declaration with the Microsoft
SDK DLL-import attribute; calls retain the operating-system import. Its
redeclaration diagnostic boundary covers only that SDK declaration. Explicit
MSVC warning-error options remain errors on both RT and Phone.

Add the generated `include` directory to `systemIncludeDirs` in the toolchain JSON.
For RT's original accessibility implementation, build the original ATL runtime:

```sh
python3 dev/windows_arm32/toolchain/prepare_atl.py \
  --output /path/to/atl --sdk-include /path/to/windows-sdk/include \
  --toolchain-config /path/to/toolchain.json
```

The command downloads hash-pinned Microsoft ATL headers and source packages,
compiles all four original source units, and produces `lib/libatls.a`. Existing
packages can be passed with `--headers-package` and `--source-package`. The
runtime preinclude retains the complete public declarations while the original
library source selects its implementation context; application classes are not
excluded. The C++ exception-state bridge calls the SDK's GNU C++ ABI runtime.
Add `/path/to/atl/include` after the SDK include directory, and add
`/path/to/atl/lib` to `rtLibraryDirs`. Phone does not use this RT library search
path. This rebuilds the SDK runtime from source and copies no operating-system
DLL. SDK redistribution licensing and the full release build remain to be
verified before publication.
The scoped Windows header also restores declarations for existing Windows app
imports used by unchanged upstream code. It does not implement fake process
operations or grant additional OS permissions. Phone process-launch behavior
still needs runtime validation. The linker generates an import archive for
`QueryInformationJobObject` using the Mobile job API contract. This archive
contains no OS implementation or system DLL. The Phone linker also generates
an import archive for the real `K32EnumProcessModules` export in KernelBase.
This keeps Dart's upstream Windows process-symbol lookup intact. On the tested
Mobile device, opening the current process with query/read access, enumerating
its modules, and closing the process handle succeeded; both executable and
separately loaded DLL exports were found. The source-built engine also passed
a physical Dart `DynamicLibrary.process()` test: it called executable and
separately loaded DLL exports, found an engine export, and rejected a missing
symbol. The same fixture verified `DynamicLibrary.executable()` and displayed
through the hardware Adreno adapter. Both Windows lookup source files are
unchanged from upstream; the Native Assets resolver's separate runtime path
still needs a dedicated test.

Configure either target through the standard engine GN entry point:

```sh
python3 dev/windows_arm32/configure_engine.py --platform rt \
  --output /path/to/build --toolchain-config /path/to/toolchain.json
python3 dev/windows_arm32/configure_engine.py --platform phone \
  --output /path/to/build --toolchain-config /path/to/toolchain.json
```

With the pinned engine dependencies and host build tools prepared, build both
families using the same source checkout and compiler configuration:

```sh
python3 dev/windows_arm32/build_engine.py \
  --output /path/to/build --toolchain-config /path/to/toolchain.json --jobs 2
```

This runs the standard GN configuration separately for RT and Phone, then builds
the native engine, ANGLE, ICU data and each matching host snapshot generator.
Phone also builds the SDK OS adapter. It rejects missing/empty artifacts and
non-ARMNT runtime DLLs, and writes artifact hashes to
`engine-build-provenance.json` only after both build commands succeed.
`--verify-existing` audits already-built artifacts without recompiling and writes
`engine-artifact-audit.json`; that audit is not evidence of a fresh source build.
The command does not yet assemble a distributable Flutter SDK or publish a
release. The complete SDK distribution and release publishing remain pending.
On a host with an SDK newer than this Dart revision supports, pass
`--host-macos-sdk /path/to/installed/MacOSX.sdk` to either build/configuration
entry point. This overrides the host SDK for that build, without changing
global Xcode selection or target features. The original Dart directory source
compiled with the installed macOS 15.4 SDK; SDK 27's added `readdir_r`
deprecation annotation had stopped it under the existing warning errors.

`.github/workflows/windows-arm32-engine.yml` reuses the source compiler job and
builds RT and Phone in separate macOS jobs from the same workflow revision.
It synchronizes the pinned DEPS through the upstream gclient setup, prepares
the Windows SDK/ATL inputs, then builds each engine with the shared compiler
configuration. It also generates the matching platform dill files and compiles
the frontend server from the pinned Dart checkout using its host Dart SDK.
The CI-only checkout omits unrelated platform SDK downloads;
engine feature flags remain unchanged. Verified output allowlists, source
manifest hashes, license notices and checksums accompany the engine artifacts.
The workflow is configured and its YAML parses, but has not run on GitHub.
These engine artifacts alone are not a complete installable Flutter SDK.
After both matrix builds succeed, the workflow combines their archives using
`merge_engine_artifacts.py`. It checks each artifact against its build record,
then requires identical framework revisions, dependency patch hashes, toolchain
configurations and platform dill bytes. Both matching frontend source records
are retained; the RT frontend snapshot is used for the common SDK compiler.
Only the engine allowlists and license notices are copied. The combined archive
has the `out/win_release_arm_{rt,phone}` layout expected by the SDK assembler.
This job has passed local YAML/shell/Python syntax checks; production artifact
merging still requires a real Actions run.

`assemble_sdk.py` integrates both engine families and the prepared native,
Cargo and Phone build settings into an existing staging SDK. All host compiler,
Windows header, Rust, Python and Appx packager dependencies must first be
staged inside that SDK. Its preflight rejects missing/external paths and
absolute nested Cargo references. It requires matching source-build records
for the engines, frontend and platform dill inputs. It does not download those
inputs, sign application packages, publish a release or claim a relocated
application build has passed.

For a Python venv, assembly validates that both its interpreter target and its
launcher directory are inside the staging SDK, then retains the venv launcher
path. Resolving that path before execution would select the base interpreter
and lose the venv's packaging dependencies. Interpreter-path validation does
not replace auditing Python's native libraries and standard library for the
complete distribution.

The locally validated C++/WinRT projection uses version `2.0.240405.15`,
generated from upstream commit `d2a66776bb16e1da9dac60770c977e847485e24b`.
Its 784 upstream source files match the pinned source archive (SHA256
`efe2adf8207d2ae56d3ddd75b8a15fc13fbae9afa200db3a4c34316e70cadb4d`).
The Windows SDK package's bundled projection is version `2.0.220110.5`;
substituting it is not covered by the current application validation.
`toolchain/prepare_cppwinrt.py` now builds the matching generator from its
checksum-pinned source archive and the upstream-pinned winmd reader, using
the fixed Windows SDK package's 88 metadata inputs. All 1,373 generated
headers matched the working projection byte for byte. The engine workflow's
`cppwinrt` job packages the projection, debugger visualization, provenance
and licenses separately as `windows-arm32-cppwinrt`; it includes no runtime
DLLs or metadata binaries. The actual workflow packaging block passed locally;
GitHub execution and integration into the complete SDK remain pending.

`toolchain/prepare_msix.py` builds the macOS Appx packager in a private checkout
of upstream MSIX commit `25a65f5c1690930813bcc10cdf1d59fa865f2bb1`. Packaging,
bundle support and XML schema validation remain enabled. The tool loads its own
`libmsix.dylib` through relative loader paths; OS libraries are referenced and
not copied. Source construction occurs in the output directory, including
upstream zlib's removal of its pre-generated `zconf.h` in favor of the
build-configured header. Shared source checkouts are not modified.

The freshly built tool successfully packaged the complete LocalSend Phone
bundle after relocation to a path containing spaces; every payload file in
the resulting Appx matched its input. The workflow's MSIX relocation and
archive steps also passed locally. Its `windows-arm32-msix-macos-arm64`
artifact contains the packager, its own library, source records and licenses.
Actual GitHub execution and complete SDK assembly remain pending.

`toolchain/prepare_python.py` prepares the checksum-pinned macOS ARM64 CPython
3.14.6 standalone runtime and Pillow 12.3.0 wheel without installing into a
shared Python environment. The runtime retains its standard library, native
extensions and upstream dependency licenses. Local installer origin paths are
removed or replaced with the fixed upstream wheel URL. External symlinks are
rejected and native library imports are recorded; system libraries are not
copied. The `python` Actions job checks file hashes and the complete inventory
before and after relocation, then packages `windows-arm32-python-macos-arm64`.
Local relocation successfully imported SSL, SQLite, ctypes, compression,
Tkinter and Pillow and decoded the original application icon. Full SDK
assembly and GitHub execution remain pending.

`toolchain/prepare_rust_distribution.py` copies the complete upstream component
manifests for Rustc, Cargo, the host standard library and `rust-src` from the
selected installation. It validates the compiler version and host, rejects
external component paths, materializes links, preserves component licenses,
and verifies copied files against the unchanged installation. Native imports
are audited for the macOS ARM64 host. It does not copy unrelated installed
target components or replace the shared Rust installation.
The Rust 1.93.1 distribution passed a real offline Cargo build and execution,
plus Rustdoc generation, after relocation to a path containing spaces. All
2,390 component files retained their upstream hashes. This is a host tool
relocation check; RT/Phone standard-library preparation and full SDK builds
must still be connected to the contained compiler, and the stable toolchain
used by application dependencies must also be staged.
The workflow's Rust matrix prepares versions `1.95.0` and `1.93.1` in
workspace-owned `RUSTUP_HOME` and `CARGO_HOME` directories using the official
Rustup 1.28.2 installer, whose SHA-256 is fixed in the workflow. The installer
does not alter shell profiles or a shared Rustup home. Each version is packaged
separately with component file hashes and licenses. Rust 1.95.0 and its matching
source have also been acquired in a private local installation. Full GitHub
execution and integration into the final distributed SDK remain pending.
The private installation follows the [official Rustup installation
configuration](https://rust-lang.github.io/rustup/installation/index.html).

Rust 1.95.0 standard-library preparation binds registry references to
`compiler_builtins` to the same in-tree crate already used by `std`. This
one-line SDK-owned Cargo manifest patch prevents an additional registry version
from making Cargo's `build-std` package-name lookup ambiguous. The intrinsic
implementation and standard-library features remain unchanged; the shared Rust
installation and application manifests are untouched. The normal LocalSend RT
build passed with this dependency binding.

SDK assembly passes the contained Python interpreter to `prepare_cmake.py`.
Its compiler, resource and Cargo launchers use relative interpreter paths
and prepend that interpreter's directory to their child process PATH. The
development default remains `python3` when no interpreter is supplied.
An actual ARM32 compilation passed after moving the generated toolchain to a
path containing spaces, quotes and a dollar sign, with a failing host Python
placed first on the original PATH. The SDK interpreter ran and the host
Python was never invoked; this does not yet prove complete SDK relocation.
The snapshot cache installer also accepts the selected interpreter and SDK
assembly supplies it. Its generated `gen_snapshot` launcher uses a relative
interpreter path and retains that Python on child PATH. After moving a fresh
cache, the launcher invoked the source-built generator and returned exactly
the same version output as the raw generator with the host Python made to
fail. This verifies the launcher routing, not a new complete AOT build.

`configure_sdk_dependencies.py` connects already staged compiler, GNU Windows
sysroot, Windows SDK, ATL, C++/WinRT, Python, packager, native Rust dependencies
and both Rust distributions. Inputs must reside in the selected SDK. It checks
compiler/Rust/Python distribution hashes and dependency links, creates separate
SDK standard-library source trees and target adapters, and writes relative
native and Cargo settings. The host standard-library links point to the
contained Rust distributions. The generated Rustup launchers explicitly use
the selected SDK's Cargo dispatcher rather than the preparation script's
source checkout. Their compiler provenance paths are also relative.

The output `sdk-dependencies.json` can be passed to `assemble_sdk.py` using
`--dependency-config`; the existing explicit dependency arguments are still
supported. External output locations are rejected before writes. The explicit
Cargo dispatcher selection passed an actual original Cargokit ARM32 DLL build,
and the new CLI interfaces and Python syntax passed local checks. Full
configuration with the production compiler and Rust 1.95.0 distribution, then
SDK assembly and relocated application builds, remain unverified.

The workflow's `sdk` job now consumes this run's compiler, engine, C++/WinRT,
MSIX, Python and Rust artifacts. Archive checksums are verified before
extraction. Windows SDK and ATL input packages are downloaded outside the
SDK; only the prepared headers, source and built ATL archive are placed in
it. The job invokes dependency configuration and assembly, moves the SDK to a
path containing spaces, then uses ordinary `flutter build windows` commands
for both families against unmodified LocalSend commit
`5ccc6dea192d1c697c2602bf456b2eb2ad8e9674`. Pub lock enforcement and a Git diff
check protect the original application sources. Successful commands record
build/relocation validation before SDK packaging; `releaseReady` remains
false pending physical validation of those CI outputs. The workflow's job
graph, shell blocks and embedded Python passed local parsing and syntax
checks. The complete job has not yet run on GitHub.

Generated Phone manifests now default to `internetClientServer` and
`privateNetworkClientServer`, preserving Windows client/server socket use
across public and private networks. Explicit application capability settings
still override these defaults. Microsoft's [capability documentation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)
describes the client/server and LAN access. Actual generated manifests passed
both default and explicit client-only checks. This uses the permissions
already exercised by the successful Phone transfer verification; it does not
change application source or add device firewall rules.

```sh
python3 dev/windows_arm32/assemble_sdk.py \
  --sdk /path/to/staging-sdk --engine-build /path/to/build \
  --host-cpu arm64 --llvm-bin /path/to/staging-sdk/toolchain/compiler/bin \
  --frontend-server /path/to/frontend/frontend_server_aot.dart.snapshot \
  --patched-sdk /path/to/build/out/win_release_arm_rt/flutter_patched_sdk \
  --toolchain-config /path/to/staging-sdk/toolchain/native.json \
  --cppwinrt-include /path/to/staging-sdk/toolchain/cppwinrt \
  --cargo-build-configuration /path/to/staging-sdk/toolchain/cargo-build.json \
  --python /path/to/staging-sdk/toolchain/python/bin/python3 \
  --packager /path/to/staging-sdk/toolchain/msix/bin/makemsix \
  --packager-kind makemsix
```

The assembly entry point has been checked for early rejection without changing
the existing SDK's toolchain files. Successful full assembly and release
validation remain pending. The generated script launchers also require Python
3 and CMake on the host's PATH.

`configure_engine.py` generates a build-local driver whose command path includes
the selected configuration path and contents. Switching toolchain settings
therefore invalidates Ninja compile commands instead of relying on environment
variables alone. Shared compiler installations remain unchanged.

For a local source build, add `--compiler-platform` to `build_engine.py`, then
build the frontend using the package configuration generated by gclient:

```sh
python3 dev/windows_arm32/build_frontend.py \
  --dart engine/src/flutter/prebuilts/macos-arm64/dart-sdk/bin/dart \
  --output /path/to/frontend
```

The frontend builder verifies that its core packages resolve to this Dart
checkout and that the port's dependency patch hashes match. Its provenance
records the source revision and the compiler/output hashes. Existing cached
frontend snapshots are not substituted for this source build.
Local source validation has now completed the upstream Dart package-config
generation, frontend AOT compilation and GN `strong_platform` generation. A
small Dart program compiled with these newly generated compiler inputs into
Kernel, then AOT using the matching source-built RT generator. The SDK's
fixed-slot Thumb-2 conversion and byte-integrity checks passed. This verifies
that compiler chain, not a complete SDK distribution, physical execution of
that fixture or an Actions run.
The corresponding Phone GN platform build also completed, and its
`platform_strong.dill` and `vm_outline_strong.dill` are byte-identical to RT's
outputs from the same source checkout.

Install those engine outputs and explicitly supplied matching compiler artifacts
into a staging SDK's cache with:

```sh
python3 dev/windows_arm32/install_engine_cache.py \
  --output /path/to/sdk/bin/cache/artifacts/engine \
  --engine-build /path/to/build --host-cpu arm64 \
  --llvm-bin /path/to/sdk/toolchain/compiler/bin \
  --frontend-server /path/to/frontend_server_aot.dart.snapshot \
  --patched-sdk /path/to/flutter_patched_sdk --require-contained-tools
```

The installer copies the explicit RT/Phone runtime allowlists, the matching RT
snapshot generator, SDK Thumb-2 conversion scripts, and upstream C++ wrapper
sources. Phone runtime files reside in `windows-arm-release/phone`. It records
installed file hashes and generates a relative AOT tool configuration without
a machine-specific workspace restriction. It does not copy system DLLs or
application sources. For a distribution, the LLVM tools must be inside the SDK;
local development may omit `--require-contained-tools` to use an external tool
installation. This command does not build the supplied frontend/platform files,
install the CMake/Rust toolchains, or configure `phone-build.json`; those remain
separate preparation steps. Installing this cache alone is not a complete SDK
distribution or proof of its relocation across machines.

`--print-command` shows the command without preparing sources or running GN.
The command prepares the pinned native dependency sources, keeps standard engine
feature defaults, and selects the platform's compiler driver. Phone selects the
UWP embedder and API family; RT retains the Win32 desktop embedder. Target build
directories are separate. Set `FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG` to the same
JSON path when invoking Ninja. The Phone engine, ANGLE, and OS adapter have passed full source builds through
this entry point. On the tested Mobile OS, the ANGLE hardware path initializes
a Qualcomm Adreno 304 device. LocalSend starts with a source-built matching snapshot generator and verified
Thumb-2 AOT, renders in Japanese, and opens the native folder picker. Recursive
listing of a selected three-file fixture passed. The RT engine and ANGLE also
passed a full source build with upstream accessibility sources unchanged and
explicit MSVC warning errors retained. Physical RT validation of these newly
built artifacts confirmed a native engine load, Japanese LocalSend rendering,
and its actual NVIDIA Tegra 4 adapter with software flags clear and a rendered
Flutter frame. The freshly rebuilt RT generator produced the same verified
Thumb-2 AOT bytes as the matching Phone generator. The original runner also
completed host shutdown with exit code zero after the diagnostic requested
WM_QUIT on its owned main thread. The original runner received a 65,597-byte
file with a Japanese filename and directory hierarchy, and selected and sent
a file through the native file picker. Both transfers matched all source bytes;
the receiver peer enforced session ID, token and declared size. This covers
those tested paths, not every native plugin or OS capability. All test tasks,
LAN-only firewall rules and transferred fixtures were removed afterwards.
Skia's native WGL factory resolves its real WGL functions from the system DLL it
already loads, retaining GL and GLES interface assembly when available. This
removes the eager OpenGL32 import on RT while keeping the existing backend.
The factory compiles for ARM32 and x64; a supported desktop WGL runtime check
remains to be completed.

The generic CMake compiler entry point lives in `toolchain/cmake_driver.py`.
It delegates to the same SDK compiler driver, preserves explicit warning-error
options and adapts command-line definitions and Unicode entry-point selection.
It performs no application-source overlay or package-specific rewrite.
Prepare its CMake entry points and compiler configuration with:

```sh
python3 dev/windows_arm32/toolchain/prepare_cmake.py \
  --output /path/to/sdk/toolchain --toolchain-config /path/to/toolchain.json \
  --cppwinrt-include /path/to/cppwinrt
```

The configuration records dependency paths relative to the generated directory.
`rt-cross.cmake` supplies the compiler, archiver and resource compiler. The
resource adapter normalizes Windows resource paths in temporary compiler input,
preserving the project's resource source. A CMake consumer build has verified
ARMNT code, a four-byte pointer ABI, Unicode entry selection and a linked
STRINGTABLE resource. Full Flutter SDK packaging and relocation of all bundled
dependencies still require validation.
The SDK project hook invokes Flutter's Unix backend for the unchanged Windows
template and declares its import-library producer for Ninja. A stock project
created with `flutter create --platforms=windows` passed
`flutter build windows --release --target-platform windows-arm` through these
public adapters, using the freshly source-built RT engine and snapshot
generator. Twelve original Windows template files matched their generated
sources, and Thumb-2 integrity checks passed. This verifies the stock RT build
path; Phone Appx generation is covered below. A complete distributable SDK
still requires integration and validation.
The existing file-selector package's HRESULT conversion is a separate native
source issue: a reduced equivalent fails with MSVC 19.40/19.44 in C++17/20 as
well as Clang. Its previously approved minimal value-conversion patch has not
yet been retired; it must not be represented as a compiler incompatibility.
Phone resolves the newer compositor dispatcher-queue function at runtime,
allowing CoreWindow rendering on Mobile systems without that export while
retaining the original API call when the OS supplies it. Other targets retain
the upstream binding.

## Distribution status

`windows-arm32-release.yml` publishes the SDK from an explicitly selected
successful source-engine workflow run on `windows-arm32-3.38.10`. The run must
belong to the same repository. Its archive checksum is checked before
`verify_release.py` compares the runtime DLL bytes with a physical validation
record. Existing release tags are preserved and releases with the same tag
are serialized. The new workflow and verifier have passed local syntax and
synthetic schema/rejection tests; no release has been published.

The physical record has `schemaVersion: 1`, `buildRunId`, `sourceRevision`,
`sdkArchiveSha256` and a `devices` object with `rt` and `phone` entries. Each
device entry contains `hardwareAccelerated`, `softwareAdapter`, `adapterName`,
`verifiedChecks` and `runtimeSha256` keyed by the runtime DLL filenames. RT
requires startup, send and receive checks. Phone additionally requires touch
alignment, folder selection and URL launching. All values must come from
actual validation of those runtime bytes; the verifier checks record/archive
consistency and does not itself operate a device or establish GPU behavior.
The SDK must also carry successful build/relocation validation, ARM32 runtime
images and no copied OS libraries. The release includes the physical record
and a separate release-verification report alongside the SDK and checksum.
The SDK's embedded build report remains a record of its pre-physical-validation
packaging stage; the attached release report records subsequent validation.

Public source review restored the original FML thread naming implementation.
The patched compiler compiled both Windows exception-handling forms used by
the thread-name debugger exception; an RT hardware probe caught the exception
and the exact `EXCEPTION_CONTINUE_EXECUTION` handler returned normally. Both
owned remote probe directories were removed. Thread naming is no longer
skipped on ARM. Windows ARM32 now first resolves `SetThreadDescription` from
KernelBase and uses the original debugger-exception path if the API is absent
or fails. The API and its matching getter succeeded on Phone 87 and returned
the actual assigned name from a Phone-contract native probe. This follows
[Microsoft's documented runtime linking requirement](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreaddescription)
for Windows 10 version 1607; other architectures retain their existing path.
The subsequent Phone runtime passed application startup, Japanese rendering,
hardware presentation and bidirectional LAN transfer validation.

The initial frame request moved from the common RuntimeController to the
Phone host after its initial metrics. RuntimeController and the original
Flutter tool backend shell script are now byte-identical to upstream.
ANGLE's fast presentation selection is restricted to ARM32, and bytecode
loading eligibility is restricted to ARM32 Windows. The engine source
manifest matches these edits. Both complete engine source builds and normal
application CLI builds subsequently passed, with physical startup and transfer
checks on both families.

A further source cleanup restores upstream `fml/message_loop.cc` and
`fml/memory/task_runner_checker.cc`, removing 17 lines of automatic message-loop
initialization from generic checks. Both candidate engine builds passed. RT
startup, Japanese rendering and settings input/redraw passed with this cleanup;
Phone startup, Japanese rendering and the first hardware presentation passed
with the installed DLL hash verified. The RT normal CLI build passed with the
candidate runtime hash verified in its output bundle. Phone input/redraw checks
remain pending. The Phone normal CLI build subsequently passed, and its signed
Appx contains the four matching runtime DLLs. Earlier transfer results do not
establish these later runtime bytes as release-ready.

The SDK Cargo dispatcher also sets `CARGO_CACHE_RUSTC_INFO=0` while redirecting
the Rust sysroot. A reused Phone build directory retained an old host-sysroot
probe in `.rustc_info.json` even though its target probe used the updated SDK
sysroot. This selected the old standard-library sources and reproduced the
`compiler_builtins` ambiguity. Compiler information must be queried through
the current SDK wrapper; application sources and shared Rust installations
are unchanged. The normal Phone build passed with this setting after restoring
the SDK's host standard libraries from the verified official-input archive.

Clang resource headers and Windows target compiler-runtime libraries are
selected separately by the GNU ARM32 driver. `resourceDir` supplies the source
compiler's headers; optional `runtimeResourceDir` selects the pinned GNU SDK's
compiler runtime during linking. The source-built compiler archive supplies
headers but does not build Windows compiler-rt libraries. The CI engine job and
SDK dependency configuration therefore select the GNU SDK runtime explicitly.
An ARM32 object performing 64-bit integer division failed to link without those libraries,
then linked as ARMNT with the same object and compiler when they were selected.

The common ARM32 glyph-options override has also been removed. Its shared
size limit bypassed both distance-field and direct-mask atlas rendering for
normal-size text. Restoring upstream defaults exposed missing RT text and
font icons. The matching DLL/PDB identity was verified before resolving the
recorded exception frames: `AtlasTextOp` reached ANGLE input-layout creation,
where D3D11 raised repeated C++ exceptions. ANGLE selected its low-feature-level
vertex conversion table only for 9.3, omitting the newly supported 9.1/9.2.
Changing that selection to `featureLevel <= D3D_FEATURE_LEVEL_9_3` restored
Japanese text, font icons and settings input/redraw on RT with upstream glyph
options retained; the corresponding exception count became zero. Fresh patch
application against the pinned ANGLE files and all resulting file hashes passed.
The verified device candidate used a targeted recompile/relink. Complete Ninja
and normal CLI builds, matching Phone validation and CI outputs remain pending.

The SDK snapshot wrapper passes its generated Thumb-2 assembly directly to
LLVM's standard input. The large assembly file was previously written only
to be consumed and deleted immediately, and ran out of disk space alongside
the application's Rust build outputs. The converter retains an explicit file
mode for diagnostics. File and standard-input modes produced byte-identical
COFF objects in the ARM instruction fixture; full application verification is
pending. Instruction lowering and the existing ELF/object integrity checks
are unchanged.

The Windows public engine properties, C++ property forwarding and project
property reader are identical to upstream, retaining GPU preference and UI
thread policy. Upstream C++20 time comparisons and `UniqueObject` constraints
are also retained. The RT default remains a separate UI thread, while explicit
platform/separate policies now select their requested runner instead of being
ignored. The restored project reader and Windows engine translation units have
compiled for ARM32. The latest RT engine relink and standard LocalSend CLI build
passed, and Japanese UI rendering was checked on RT hardware with the NVIDIA
D3D driver loaded. Explicit platform-thread startup still requires physical
validation. Historical initialization thread
samples do not establish the original merged-thread stall's root cause.

The runtime GPU dependency, GPU texture source and `MethodResult` header now
match upstream. The global handler that altered registers on alignment faults
was removed; the existing OS alignment error mode remains. Normal RT startup
and rendering passed after this removal. The test process, scheduled tasks and
staging files were removed. The final runtime's transfer behavior has not yet
been revalidated; WebRTC signaling also logged a TLS handshake EOF.

This directory contains source preparation and validation components. The
portable engine/toolchain build, Phone SDK packaging, GitHub Actions and release
installer still require integration and validation. These sources alone are
not an installable SDK release.

Generated Rust target specifications refer to their SDK linker by executable
name. The SDK Cargo dispatcher adds the selected target directory to its own
process PATH; it does not change the user's environment. Moving that directory
has been checked for both RT and Phone with Rust 1.93.1: the compiler accepts
both relocated target specifications and resolves/runs the relocated linkers.
This check does not verify a complete relocated SDK or a full Cargo build.
The private Cargokit Cargo launchers and their configuration provenance also
use relative paths; a moved launcher has been checked with its old location
absent. When preparing CMake tooling for a distribution, supply
`prepare_cmake.py --sdk-root /path/to/sdk` to reject native/Cargo dependencies
outside the SDK before creating the output directory. This boundary check
does not replace a relocated application build or validate transitive Cargo
configuration and symlink dependencies.

### Generic UWP host source build

The CoreWindow host now builds from SDK-owned native sources. Rendering, input,
window metrics, CoreText, clipboard, locales, lifecycle, back navigation and
activation are retained in `engine/src/flutter/shell/platform/windows/uwp`.
Package platform channels remain in `dev/windows_arm32/plugins`, registered by
SDK-generated native build glue, separately from the engine host.

For a source-level host build with the original file-selector package contract:

```sh
python3 dev/windows_arm32/build_uwp_host.py \
  --toolchain-config /path/to/toolchain.json \
  --output /path/to/sdk-host-build \
  --plugin file_selector_windows --plugin url_launcher_windows \
  --plugin window_manager --plugin screen_retriever_windows \
  --package file_selector_windows=/path/to/original/file_selector_windows
```

The script compiles the original generated Pigeon source without modifying it,
creates a native registrant in the output directory and links an ARM32 Thumb
AppContainer executable. No OS DLLs are copied. Unsupported requested adapter
names fail explicitly. A plugin-free dependency graph can omit the plugin and
package arguments.

This source build has passed with warnings treated as errors. The standard
Flutter CLI also builds and packages the host, as described below. Physical
validation of the package-free standard Flutter counter passed AOT startup and
its first hardware-rendered Flutter frame on Mobile. Startup loads the SDK-generated `Assets/FlutterSplash.bin` asset and standard
`data` assets. The FSP1 splash payload contains explicit dimensions and background
RGBA followed by premultiplied RGBA pixels; the host has no application-specific
image dimensions or background color.

### Windows Phone CLI integration in progress

The SDK tool accepts `flutter build windows --release --target-platform windows-arm
--windows-family phone`. The desktop family remains the default and uses the
existing RT/Win32 path. Phone uses the normal Flutter release asset/AOT build
and the SDK's CoreWindow host builder, without unpacking Win32 runner artifacts.
The SDK-owned `toolchain/phone-build.json` names the Python command, toolchain
configuration and exactly four source-built runtime DLLs plus ICU data. Runtime
paths are resolved relative to that configuration, separately from application
sources. `--config-only` generates the native plugin registration without compiling.

The source CLI path has built an unmodified stock Flutter Windows project into
`build/windows/arm-phone/bundle`. The command now also generates an unsigned Appx with application metadata,
icons and splash assets. Optional developer signing has passed signature and
block-map verification. The extracted host has initialized the Adreno 304
hardware adapter and rendered its splash on Mobile. The corrected package
with `codeGeneration` passed AOT startup and its first hardware-rendered Flutter
frame for the unchanged standard counter application.
Native adapters are currently supplied for file_selector_windows,
url_launcher_windows, window_manager, screen_retriever_windows, dynamic_color,
connectivity_plus, uri_content, open_dir_windows, pasteboard, gal, desktop_drop,
bitsdojo_window_windows, windows_taskbar and tray_manager.
This is an integration inventory, not proof that every package method is
complete. The file-selector adapter currently handles the original open/folder
Pigeon method; its save-dialog method remains to be implemented. On Mobile,
real URL support queries and queued URL/picker error-reply suppression after
registration removal passed. A separate application built with the normal SDK
command verified the original folder-picker and URL-launcher Dart APIs on Mobile:
selected-folder enumeration, file reading/writing/deletion, browser activation,
and completion after returning to the application. File deletion rejected a
missing file and preserved directories and read-only files. The Phone SDK's CRT
import adapter retains the original `_wremove` call and uses OS storage-item
deletion for picker-authorized paths when that call is denied; Dart and application
deletion code remain unchanged. Other file-picker methods and the complete
LocalSend Phone behavior still need verification. The full original LocalSend
native host and Rust FFI libraries build together. Its signed release package
starts on Mobile with hardware rendering and its HTTPS listener. The complete
standard CLI invocation has now passed through signing and signature verification
in one run for the original LocalSend project, with only the separately approved
send-session correction. The verified command used `--release --no-pub
--build-number=61`; local Cargo dependency resolution was offline and limited
to one build job. The signed ARMNT package contains the engine, ANGLE, SDK OS
adapter and the two original Rust FFI modules, with no system DLL payloads.
That newly generated package has started on Mobile with a hardware-rendered
Flutter frame, Japanese UI and its HTTPS listener. The send tab responds to
injected touch, and the original application's folder button launches the OS
picker. Selecting an owned one-file folder returned to LocalSend and displayed
one selected file totaling 24 bytes. Selection clearing and picker cancellation
also returned to the send UI. Full transfer behavior for that package and all
OS integrations remain to be verified.
Missing desktop OS APIs are reported as errors, rather than fabricated native
results. Additional native plugin names fail explicitly until their UWP build
paths are integrated.
Debug/profile runtime installation is also still pending.

`platform/network_information.h` reads WLAN profiles through the official WinRT
NetworkInformation API. It preserves the adapter GUID, connected SSID,
connectivity/security enums and matching LAN infrastructure identifiers without
synthesizing legacy WLAN attributes. On the tested Mobile device, a separate
UWP host returned one WLAN profile and its matching six-byte infrastructure
identifier. The verification package was removed afterwards. Standalone console
launch failed before producing API output; it is not evidence that the WinRT
query is unavailable. The `wlanapi.dll` compatibility interface and complete
translation of its native contracts remain to be implemented. The device does
contain the real system `wlanapi.dll`: direct OS loading in a UWP host succeeds
and exposes all five functions used by network_info_plus, whereas package-only
loading fails. The SDK's exact bare-name loader now acquires that real module
without copying it or fabricating exports. Native `WlanOpenHandle` still returns
`ERROR_ACCESS_DENIED`, including in a verification package declaring
`wiFiControl`; DLL resolution and API permissions are separate issues.
The rebuilt SDK loader has not yet passed complete original-plugin runtime
integration. Its upstream IPv4
conversion also omits assigning the address before `inet_ntop`; the SDK must not
alter allocation/IP API results to compensate for that application dependency
bug.

The SDK configuration also supplies `packager` and `packagerKind` (`makeappx` for
Microsoft's Windows SDK executable, or `makemsix` for the open-source MSIX SDK
host tool). The configured Python environment needs the pinned dependencies in
`requirements-phone-packaging.txt`; these are host packaging dependencies and
are not included in the application package. Optional `packageDefaults` contains
publisher, publisherDisplayName, backgroundColor, languages and capabilities.
Package identity and version derive from the application's existing pubspec;
display name and icon derive from its Windows runner resources. Generated
metadata, registration sources, assets and Appx stay in the build directory.
No application source or original image is rewritten.

Generate the Phone configuration from installed build dependencies:

```sh
python3 dev/windows_arm32/toolchain/prepare_phone.py \
  --output bin/cache/artifacts/engine/windows-arm-release/toolchain \
  --toolchain-config /path/to/toolchain.json \
  --runtime-directory /path/to/win_release_arm_phone \
  --python /path/to/packaging-venv/bin/python \
  --packager /path/to/makemsix --packager-kind makemsix
```

The command validates the native runtime files, configured toolchain inputs and
the pinned Pillow version before writing `phone-build.json`. Use
`--package-defaults /path/to/defaults.json` for publisher and other package
defaults. It records dependency paths relative to the output directory,
including the interpreter path without resolving its virtual-environment
symlink. Flutter tools resolve that path against the configuration directory.
Relocation requires moving those dependencies together; this command neither
copies them into a release nor proves the complete SDK is portable. Configure
developer signing afterwards in the local configuration, separately from
public source and application source.

The image pipeline has verified native splash dimensions/background and exact
pixel agreement with the PNG, and the generated Appx contains only the SDK's
four runtime DLLs. No OS DLLs or private signing keys are added to the package.

The engine maps precompiled instruction pages with `VirtualProtectFromApp`.
The package generator always includes the required `codeGeneration` capability,
including when the optional capability list is supplied. Other application
permissions remain package configuration. The first physical package lacked
this capability and failed mapping executable pages with access denied; a
corrected signed package passed AOT startup and its first Flutter frame on an
FTJ152E using the Adreno 304 hardware adapter. A subsequent remote tap was
acknowledged, but the device rebooted before its result could be captured.
That input path remains unverified and the reboot cause is undetermined. Other
applications and system-bar configurations still require validation.

Optional `signing` configuration selects `osslsigncode` or `signtool`. For
`osslsigncode`, supply `tool`, `certificate` and `privateKey` paths; relative
paths resolve against `phone-build.json`. The builder signs with SHA-256 and
verifies the package against the supplied certificate. For `signtool`, supply
`tool` and `certificateThumbprint`; it uses the Windows certificate store and
verifies with the Authenticode policy. A signing failure fails the build.
Certificates must match the package publisher and be trusted on the target
device. Private keys remain local configuration inputs and are never copied
into the application package or SDK distribution. Without `signing`, the
output is an unsigned package for a separate signing step.

### Rust native linker integration in progress

`toolchain/rust_link.py` routes Rust linker arguments through the same configured
native compiler driver as Flutter. Set `FLUTTER_WINDOWS_ARM32_PLATFORM` explicitly
to `rt` or `phone` and supply `FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG`. The target
family supplies its UCRT contracts in place of the Rust MSVC legacy CRT selectors.
CMake toolchain preparation copies this entry point beside the other drivers.

A newly compiled Rust ARM32 scalar fixture linked into both target DLLs through
the configured SDK compiler, retained its exported symbol and selected the
AppContainer flag only for Phone. This fixture uses cached Rust core metadata
and no Dart VM binary; it does not verify a complete Rust standard-library,
LocalSend or rhttp build. The original application's Cargokit target lookup
recognizes Windows x64 and ARM64 but returns no target for `windows-arm`. The
SDK Cargo backend supplies the prepared target without changing that
application or its bundled build tool; complete LocalSend Rust crate builds
are described below.

For distribution, prepare Rust's matching source and host standard library
inside the SDK:

```sh
python3 dev/windows_arm32/toolchain/prepare_rust_sysroot.py \
  --rustc /path/to/rustc --output /path/to/sdk/rust-sysroot \
  --copy-host-stdlib
```

Without this option, development preparation keeps its existing link to the
installed host standard library. With it, the host files are copied into the
SDK; an existing development link is replaced only after copying succeeds.
Repeated preparation checks copied contents and refuses to overwrite modified
host files. Rust 1.93.1 host compilation and execution passed after moving this
sysroot to a path containing spaces. The Rust compiler and other SDK
dependencies must still be staged separately for a complete distribution.

Generate the Rust target definitions from the installed compiler:

```sh
python3 dev/windows_arm32/toolchain/prepare_rust.py \
  --output /path/to/sdk/rust-toolchain \
  --rustc /path/to/rustc \
  --toolchain-config /path/to/native-toolchain.json
```

The command starts from Rust's built-in `thumbv7a-pc-windows-msvc` definition,
retaining ABI layout, Thumb-2 CPU features, TLS, static-CRT controls, atomics and
debug-information capabilities. It adapts linker command-line selection and
the RT/Phone library family, and installs both linker launchers beside the
native SDK driver. Its provenance records the original compiler definition and
version. Unstable target introspection is scoped to the preparation process.
The generated definitions passed `rustc --print cfg`, including ARM32, MSVC
environment, Thumb-2 and thread-local support. Both installed launchers linked
a real Rust scalar object from a directory containing spaces.

Native dependency paths are relative to the generated configuration. Rust's
target JSON requires an executable linker path; rerun preparation after moving
the SDK dependencies. Regeneration and linking at a second location have
passed. This validates target preparation and native linking, not a fresh full
Rust standard-library or application Cargo build.

Cargokit policy integration now has an SDK-owned generated bridge:

```sh
python3 dev/windows_arm32/toolchain/prepare_cargokit_bridge.py \
  --cargokit-directory /path/to/original/package/cargokit \
  --output /path/to/application/build/cargokit-sdk-bridge.dart
```

The bridge imports the package's original public build environment, target and
Rust builder. It supplies the prepared ARM32 target name without editing the
original target lookup table, and delegates configuration, extra Cargo flags,
toolchain selection and environment policy to that builder. This is necessary
because rhttp's bundled Cargokit includes project-specific HTTP/3 configuration
that a replacement Cargo command would otherwise miss. Generated bridges for
both current packages have executed against unmodified copies of the original
Cargokit libraries. The original command-capture test hook confirmed the
logical ARM32 target, selected Cargo features, debug/release options, toolchain
arguments and rhttp HTTP/3 environment in four profile cases. This validates
command construction; it does not execute Cargo. SDK toolchain dispatch and
actual Cargo execution remain to be integrated.
The bridge is generated build output, not a modification to application Dart
or to the original package's build-tool source.

The generated bridge accepts `--copy-artifacts` after the target name to copy
the built Windows DLL and available import/debug files into
`CARGOKIT_OUTPUT_DIR`. Artifact names come from the original Cargokit library,
as in its CMake build path. Missing required DLLs fail explicitly before any
copy. Both current packages passed this copy contract with a real freshly built
ARM32 Rust DLL and the original builder's command-capture hook. This verifies
artifact copying and missing-output handling, not a complete application build.

`toolchain/cargo_toolchain.py` prepares a captured `rustup run TOOLCHAIN cargo
build` invocation for an explicitly configured SDK compiler and ARM32 target.
It retains original Cargo arguments and package Rust flags, supplies the SDK
sysroot for host and target probes, and preserves existing compiler wrappers
and encoded-flag precedence. It scopes Cargo cache and temporary paths to the
configured workspace and requires a locked dependency graph. Other targets or
uninstalled toolchain names fail before invoking Cargo.

Command forwarding has been checked with the original rhttp build command.
Actual compiler probes confirmed the SDK sysroot, including nested workspace
and existing compiler wrappers. This is not yet the installed Cargokit backend
or a complete Cargo build; native compiler environment, build-tool startup and
Rust standard-library/application compilation still need integration.

The SDK Cargo dispatcher has now performed real offline, locked builds for both
RT and Phone, rebuilding Rust's standard-library components for the prepared
targets and linking a consumer that uses `std::thread::spawn` and `join`. Both
DLLs expose the expected ARM32 Thumb symbol; only Phone is AppContainer. Native
COFF unwind records decode correctly. GNU command-line selection explicitly
avoids an ELF EH-frame header while retaining Windows unwind support. The
compiler wrapper preserves Cargo's jobserver descriptors instead of suppressing
the warning from dropped descriptors.

This verifies standard-library compilation and native consumer linking. The
consumer DLLs have now loaded and returned the expected thread result on both
the physical Surface RT device and the FTJ152E under UWP AppContainer. The
fixture calls `std::thread::spawn` and `join` and returns 43; RT also exits with
code zero. Complete LocalSend/rhttp crate builds and the installed Cargokit
backend remain pending. No application
features or source were removed for these builds.

The Unix cross-build SDK can prepare a private Rustup home for the original
Cargokit builder, which otherwise resolves the user's home-directory Rustup
before PATH. `prepare_cargokit_rustup.py --configuration CARGO_CONFIG --output
SDK_RUSTUP_HOME` installs SDK Cargo launchers for explicitly configured compiler
channels/versions. Pinned versions and compiler channels must match. This home
is scoped to the generated bridge's build command; it must not be used for
Rustup installation, component/target changes, or updates. Existing compiler
libraries are referenced without modifying them.

`cargo_backend.py` accepts `--configuration BACKEND_CONFIG
--cargokit-directory ORIGINAL_CARGOKIT --platform rt|phone build-cmake`.
The backend configuration names `dart`, `pubCache`, `rustupHome` and
`cargoConfiguration`; these paths may be relative to that file.
`pubGetOffline: true` optionally uses cached Dart dependencies. The backend
generates a build-only runner and resolves the original build-tool dependency
declarations, then invokes the original Rust builder with its package policy.
It copies Windows artifacts using the original package contract.
The original Cargokit Rustup resolver checks the user's `.cargo/bin` before
PATH. This resolver is retained: a working host Rustup installation is a build
prerequisite, while `RUSTUP_HOME` selects the private SDK toolchain launchers.
Adding a bundled executable to PATH alone does not override that resolver.

Pass `--cargo-build-configuration BACKEND_CONFIG` to `prepare_cmake.py` to
install this entry point beside the SDK CMake adapters. The SDK redirects the
original Windows Cargokit script invocation and performs its path-resolution
step on the Unix host. Other process invocations and native Windows hosts keep
their existing behavior. An actual original LocalSend Cargokit builder and the
SDK backend have run Cargo against the full Rust crate and copied its freshly
built DLL without command-capture hooks. Full application `flutter build`
integration and full application runtime verification remain pending.

The pinned LocalSend Rust crate has now built in release mode for both prepared
RT and Phone targets from the same unchanged application/core sources. Both
`cdylib` and `staticlib` outputs are retained, including HTTP, WebRTC and crypto
dependencies. Each complete DLL has loaded on its physical target and its
original Flutter Rust Bridge content-hash export returned the expected value.
The Phone test ran in a signed UWP AppContainer package with normal signature
validation. Only the owned test executable/DLL or package was deployed; no
system DLLs were included. Both devices' test staging and packages were cleaned.
This verifies complete DLL loading and a native exported function, not full
application behavior or every HTTP/WebRTC/crypto operation.

SDK dependency patches now use Cargo's `resolver.lockfile-path` mechanism,
enabled by `-Z lockfile-path` in the pinned Cargo 1.95 toolchain. The dispatcher
creates a separate lockfile below the application target directory and leaves
the original sources, manifest and lockfile in place. It resolves only the
SDK's same-version path substitutions, rejects changed dependency names or
versions and rejects any unpatched lock entry changes. Preparation and the
subsequent build share a workspace guard to prevent simultaneous preparation
from replacing an in-use SDK lockfile.

An actual release build of the complete LocalSend Rust crate has passed with
the original source tree directly, automatic SDK lock preparation, offline
dependencies and the normal locked build. All 50 original application/core
source, manifest and lock files retained their hashes. The graph retains its
383 package names and versions; only ring and three windows-sys versions use
SDK paths. Source copies are unnecessary for this dependency adaptation.
`toolchain/prepare_rust_dependencies.py` now prepares those SDK packages from
checksum-pinned crate archives and source patches. The three Windows binding
packages gain ARM32 eligibility for the existing 32-bit ABI declarations.
The ring patch adds Windows ARM assembly generation and retains NEON Poly1305;
Rust crypto algorithm and dispatch sources remain byte-identical. All files
of all four freshly prepared packages matched the existing validated SDK
packages. Upstream crate source and license files are retained.

The generated configuration is `.cargo/sdk-patches.toml`, with paths relative
to its grandparent directory as required by [Cargo's configuration
rules](https://doc.rust-lang.org/cargo/reference/config.html#config-relative-paths).
SDK lock preparation uses the same base when checking permitted package
substitutions. The SDK assembly preflight rejects absolute patch paths and
packages outside the SDK. Actual Cargo metadata and separate lock preparation
with this relative configuration preserved the original application's 383
package identities and changed only the four intended SDK package sources;
the original manifest and lockfile retained their hashes. This validates
dependency resolution, not a new full application build.

The workflow's `rust_dependencies` job downloads the fixed crate versions,
checks archive and patch hashes, prepares complete source packages, verifies
file inventory and relative paths, and uploads
`windows-arm32-rust-dependencies`. Its actual archive step passed locally
using copies of the independently checked preparation outputs. GitHub
execution and complete SDK integration remain pending.
See [Cargo's lockfile-path tracking issue](https://github.com/rust-lang/cargo/issues/14421)
for the configuration change and its version history.

The RT CMake adapter applies the approved file_selector_windows 0.9.3+3 HRESULT
correction only to a checksum-pinned source copy under `build/.../sdk-native`.
It adds `<cstdint>` and two explicit integer conversions for codec error
details. This reproduces under MSVC as well, so it is a separately identified
native package correction rather than a compiler behavior change. Original
package files and ordinary native Windows builds remain untouched.

MSVC warning-profile translation retains errors for unused local variables and
later explicit diagnostic requests while accounting for Clang-only style
diagnostics. Microsoft extension C4596 remains off by default as in MSVC;
`/we4596` enables its corresponding error. Existing
`_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING` definitions are honored for
the original conversion classes through an SDK-owned libc++ compatibility
header, without disabling unrelated deprecated declarations. Compiler fixtures
have verified these positive and negative cases. See
[MSVC C4596](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/c4596)
and [Microsoft's STL declaration macros](https://github.com/microsoft/STL/blob/main/stl/inc/yvals_core.h).
The complete original LocalSend C++ plugin build currently reaches the unused
anonymous-namespace function diagnostic in url_launcher_windows; broad
unused-function suppression has not been applied, and the full application
build has not yet passed.
