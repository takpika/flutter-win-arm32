#!/usr/bin/env python3
"""Configure an ARM32 engine through Flutter's standard GN entry point."""
import argparse
import hashlib
import json
import os
import platform as host_platform
from pathlib import Path
import shlex
import subprocess
import sys

from prepare_sources import prepare_sources


def command(platform, output, host_macos_sdk=None, driver=None):
    port = Path(__file__).resolve().parent
    root = port.parents[1]
    driver = driver or port / 'toolchain' / f'{platform}_driver.py'
    arguments = [sys.executable, str(root / 'engine/src/flutter/tools/gn'),
                 '--target-os=win', '--windows-cpu=arm', '--runtime-mode=release',
                 '--no-enable-unittests', '--no-rbe', '--ide=',
                 '--out-dir=' + str(output), '--target-dir=win_release_arm_' + platform]
    # Flutter fetches these dependencies beside ANGLE, rather than inside it.
    settings = {f'angle_vulkan_{name.replace("-", "_")}_dir':
                f'//flutter/third_party/vulkan-deps/vulkan-{name}/src'
                for name in ('headers', 'loader', 'tools', 'validation-layers')}
    # Match --no-enable-unittests for the packaged release; retain all backends.
    settings['angle_build_tests'] = False
    # Use the OS shader compiler; distributions must not copy system DLLs.
    settings['angle_copy_d3dcompiler_dll'] = False
    settings['skia_use_freetype'] = True
    settings['skia_enable_fontmgr_custom_empty'] = True
    if host_platform.system() == 'Linux':
        # Host tools use the container's development headers. The Windows
        # target still uses the explicitly selected Windows cross toolchain.
        settings['use_default_linux_sysroot'] = False
    if host_macos_sdk is not None:
        sdk = host_macos_sdk.resolve()
        if not (sdk / 'usr/include').is_dir():
            raise ValueError('Missing macOS host SDK headers: ' + str(sdk))
        settings['mac_sdk_path'] = str(sdk)
    if platform == 'phone':
        # UWP uses the embedder API rather than the Win32 desktop shell.
        arguments += ['--embedder-for-target', '--disable-desktop-embeddings']
        settings.update(flutter_windows_phone=True, flutter_phone_tool=str(driver),
                        angle_is_winuwp=True, skia_enable_winuwp=True,
                        skia_gl_standard='gles')
    else:
        settings['flutter_windows_arm32_tool'] = str(driver)
        settings['angle_enable_d3d11_low_feature_levels'] = True
    arguments += ['--gn-args=' + ' '.join(
        f'{key}={json.dumps(value)}' for key, value in settings.items())]
    return root, arguments


def configured_driver(platform, output, configuration):
    # Ninja must see configuration changes in the command, rather than only
    # in the environment. Keep the shared driver and compiler untouched.
    identity = hashlib.sha256(str(configuration).encode() + b'\0' +
                              configuration.read_bytes()).hexdigest()
    destination = output / '.windows-arm32-cache/drivers' / (platform + '-' + identity + '.py')
    original = Path(__file__).resolve().parent / 'toolchain' / f'{platform}_driver.py'
    content = ('import os, runpy, sys\n'
               f'os.environ["FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG"] = {str(configuration)!r}\n'
               f'sys.path.insert(0, {str(original.parent)!r})\n'
               f'runpy.run_path({str(original)!r}, run_name="__main__")\n')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_text() != content:
        destination.write_text(content)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['rt', 'phone'], required=True)
    parser.add_argument('--output', type=Path, required=True,
                        help='Build root; GN creates out/win_release_arm_<platform> here')
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--host-macos-sdk', type=Path,
                        help='Select an installed host SDK for this build without changing Xcode settings')
    parser.add_argument('--print-command', action='store_true',
                        help='Show the command without preparing sources or running GN')
    args = parser.parse_args()
    root, arguments = command(args.platform, args.output.resolve(), args.host_macos_sdk)
    configuration = args.toolchain_config.resolve()
    if args.print_command:
        print('FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=' + shlex.quote(str(configuration))
              + ' ' + shlex.join(arguments), flush=True)
        return 0
    if not configuration.is_file():
        parser.error('Toolchain configuration does not exist: ' + str(configuration))
    driver = configured_driver(args.platform, args.output.resolve(), configuration)
    root, arguments = command(args.platform, args.output.resolve(), args.host_macos_sdk, driver)
    print(shlex.join(arguments), flush=True)
    prepare_sources(root)
    scratch = args.output.resolve() / '.windows-arm32-cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(configuration),
                       TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch))
    return subprocess.call(arguments, cwd=root / 'engine/src', env=environment)


if __name__ == '__main__':
    sys.exit(main())
