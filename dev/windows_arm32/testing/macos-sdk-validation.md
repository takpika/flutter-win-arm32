# Native macOS SDK validation

The Ubuntu workflow produces the complete SDK but cannot execute Mach-O tools.
Its `cross-prepared` artifact therefore retains false native-execution and
application-build fields. Compiler, AOT-generator and MSIX component tests do
not establish that the complete SDK can build an application.

On macOS, download the host's successful `cross-prepared` artifact and verify its
`.tar.gz.sha256` file before extraction. Use a disposable directory on a volume
with room for the SDK, application outputs, and the protected 512 MiB reserve.
Keep an existing shared compiler or Flutter checkout outside this directory.

```sh
work="$PWD/_work/macos-validation"
mkdir -p "$work"
shasum -a 256 -c flutter-windows-arm32-macos-arm64-cross-prepared.tar.gz.sha256
tar -xzf flutter-windows-arm32-macos-arm64-cross-prepared.tar.gz -C "$work"
git clone https://github.com/localsend/localsend.git "$work/localsend"
git -C "$work/localsend" checkout 5ccc6dea192d1c697c2602bf456b2eb2ad8e9674

python3 dev/windows_arm32/testing/validate_macos_sdk.py \
  --work "$work" --sdk "$work/flutter" \
  --relocated-sdk "$work/移動した Flutter SDK" \
  --application "$work/localsend" --host macos-arm64 \
  --sdk-archive-sha256 ORIGINAL_ARCHIVE_SHA256 \
  --output "$work/native-sdk-validation.json"
```

Replace `ORIGINAL_ARCHIVE_SHA256` with the verified checksum. For the x64 SDK,
use its archive and `--host macos-x64`. On Apple Silicon that test executes the
actual x64 tools through Rosetta; it does not substitute arm64 executables.
Set `PUB_CACHE` to an existing cache if desired. All new temporary files and
Cargo output remain in the work directory. Git, CMake, Ninja, and support for
the chosen execution architecture are host prerequisites.

The helper generates LocalSend's existing MSIX helper prerequisite and runs
the original application's RT and Phone release builds. It moves the SDK to
the Unicode path, verifies that the original path is unavailable, and repeats
both builds. Application and SDK tracked-source diffs must remain empty.
Failed commands leave their logs and produce no successful validation record.

Put the resulting object in `validation.nativeSdkValidation` for that host's
`sdk_records` entry in the four-host publishing workflow. Its four command
results and log hashes identify the initial/relocated RT/Phone builds. The
publisher requires the exact archive checksum, source revision and host, plus
the separate hardware GPU and physical-device checks. It preserves the archive
bytes and the original CI metadata; the release evidence records the later
native verification. macOS tar archives are renamed for distribution without
repacking. Large archives use the lossless numbered release transport.
