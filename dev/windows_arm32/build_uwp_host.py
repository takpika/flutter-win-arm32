#!/usr/bin/env python3
"""Build the SDK's CoreWindow host and selected native package adapters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plugin', action='append', default=[])
    parser.add_argument('--cppwinrt-include', type=Path)
    parser.add_argument('--config-only', action='store_true')
    parser.add_argument('--package', action='append', default=[], help='NAME=PACKAGE_DIRECTORY')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    configuration = args.toolchain_config.resolve()
    output = args.output.resolve()
    packages = dict(entry.split('=', 1) for entry in args.package)
    adapters = {
        'file_selector_windows': ('file_selector_windows', 'RegisterFileSelector'),
        'url_launcher_windows': ('url_launcher_windows', 'RegisterUrlLauncher'),
        'window_manager': ('window_manager', 'RegisterWindowPlugins'),
        'screen_retriever_windows': ('window_manager', 'RegisterWindowPlugins'),
        'dynamic_color': ('dynamic_color', 'RegisterDynamicColor'),
        'connectivity_plus': ('connectivity_plus', 'RegisterConnectivity'),
        'uri_content': ('uri_content', 'RegisterUriContent'),
        'open_dir_windows': ('open_dir_windows', 'RegisterOpenDirectory'),
        'pasteboard': ('pasteboard', 'RegisterPasteboard'),
        'gal': ('gal', 'RegisterGallery'),
        'desktop_drop': ('desktop_drop', 'RegisterDesktopDrop'),
        'bitsdojo_window_windows': ('bitsdojo_window_windows', 'RegisterBitsdojoWindow'),
        'windows_taskbar': ('windows_taskbar', 'RegisterWindowsTaskbar'),
        'tray_manager': ('tray_manager', 'RegisterTrayManager'),
    }
    # This checksum-pinned upstream Windows backend implements no native methods.
    # The generic host's unimplemented response is its complete original contract.
    unimplemented = {
        'permission_handler_windows': ('windows/permission_handler_windows_plugin.cpp',
            '0360687dde3812ce74637bc550d404e15e3cea07bf52fc882c7f409672e57293'),
    }
    for name in set(args.plugin) & unimplemented.keys():
        if name not in packages:
            parser.error('Supply the original Windows package directory: ' + name)
        relative, checksum = unimplemented[name]
        original = Path(packages[name]) / relative
        if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != checksum:
            parser.error('This package has a different native contract; an adapter is required: ' + name)
    unknown = set(args.plugin) - adapters.keys() - unimplemented.keys()
    if unknown:
        parser.error('Native UWP adapters are not installed for: ' + ', '.join(sorted(unknown)))
    selected = sorted({adapters[name] for name in args.plugin if name in adapters})
    native_window_adapters = {'bitsdojo_window_windows', 'tray_manager', 'windows_taskbar', 'window_manager'}
    native_window = any(directory in native_window_adapters for directory, _ in selected)
    include = []
    if {'open_dir_windows', 'pasteboard', 'gal', 'desktop_drop'} & set(args.plugin):
        if args.cppwinrt_include is None or not (args.cppwinrt_include / 'winrt/base.h').is_file():
            parser.error('Supply the SDK C++/WinRT projection for these native adapters')
        include += ['-I', str(args.cppwinrt_include.resolve())]
    sources = []
    if 'tray_manager' in args.plugin:
        if 'tray_manager' not in packages:
            parser.error('Supply the original tray_manager package directory')
        original = Path(packages['tray_manager']).resolve() / 'windows/tray_manager_plugin.cpp'
        if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != \
                'aaae9fe0426c6e0fbd357a6f367e1d9b2005b61aecc853cabc1585dded7b91bf':
            parser.error('This tray_manager version requires reviewing its native contract')
        sources += [('tray_native_api', root / 'dev/windows_arm32/plugins/tray_manager/native_api.cpp')]
    if 'windows_taskbar' in args.plugin:
        if 'windows_taskbar' not in packages:
            parser.error('Supply the original windows_taskbar package directory')
        directory = Path(packages['windows_taskbar']).resolve() / 'windows'
        contract = {
            'windows_taskbar_plugin.cc': '15ac0588ce142ce7c8c52532508f3d6c4431fddaee482e5bc9e3992bb0fa1f0b',
            'windows_taskbar.h': '900bba4f586f6190d423f0b206c73e45197573a8d8191478d553c09a19324216',
            'windows_taskbar.cc': 'd257915ccff9747b59b474041b05387502cdea270260c71892476aff798df745',
            'utils.h': '637f4e0006a2ff120dfc25d67e7ad83e6319876e0fd751dbb33c594076113d07',
            'utils.cc': '542e5b9c2a168040fcf16811b492a09ddd3f073d365f7232400932e70da9b244',
        }
        for name, checksum in contract.items():
            original = directory / name
            if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != checksum:
                parser.error('This windows_taskbar version requires reviewing its native contract: ' + name)
        include += ['-I', str(directory)]
        sources += [
            ('taskbar_original_utils', directory / 'utils.cc'),
            ('taskbar_native_api', root / 'dev/windows_arm32/plugins/windows_taskbar/native_api.cpp'),
        ]
    if 'bitsdojo_window_windows' in args.plugin:
        if 'bitsdojo_window_windows' not in packages:
            parser.error('Supply the original bitsdojo_window_windows package directory')
        directory = Path(packages['bitsdojo_window_windows']).resolve() / 'windows'
        contract = {
            'bitsdojo_window_api.cpp': '40c7787117193e88e1247396dcda132c50c650417dccc88affc66c7bd3c640aa',
            'bitsdojo_window_api.h': '6618796253aa140338820a9f5b70b42cc78442d30c56c05db63ed59883d91dfc',
            'bitsdojo_window.h': '92a0f1e170c77cf30f93c85d59b52daae2b9a39c0b77766ec7c12f57355496d8',
            'bitsdojo_window_common.h': 'aaee9b0b1cd70c8ef7f4e5cee2a549ed9a2bc2266f1f7cd4e142b828c5e988af',
            'window_util.h': 'c76ca6a0c39018038d5c39dbf859a0815e8410f2056afc699af596f2fecebb2f',
        }
        for name, checksum in contract.items():
            original = directory / name
            if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != checksum:
                parser.error('This bitsdojo version requires reviewing its native ABI: ' + name)
        include += ['-I', str(directory)]
        sources += [
            ('bitsdojo_original_api', directory / 'bitsdojo_window_api.cpp'),
            ('bitsdojo_native_api', root / 'dev/windows_arm32/plugins/bitsdojo_window_windows/native_api.cpp'),
        ]
    if 'file_selector_windows' in args.plugin:
        if 'file_selector_windows' not in packages:
            parser.error('Supply the original file_selector_windows package directory')
        directory = Path(packages['file_selector_windows']).resolve() / 'windows'
        for name in ('messages.g.h', 'messages.g.cpp'):
            if not (directory / name).is_file():
                parser.error('Missing original generated package contract: ' + str(directory / name))
        include += ['-I', str(directory)]
        sources.append(('file_selector_messages', directory / 'messages.g.cpp'))
    output.mkdir(parents=True, exist_ok=True)
    if 'gal' in args.plugin:
        if 'gal' not in packages:
            parser.error('Supply the original gal package directory')
        original = Path(packages['gal']) / 'windows/gal_plugin.cpp'
        if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != \
                '796c793ef6b16f954dbd9f3b83ad31038d9f33a4792df7e6de11eab8ee3d6c31':
            parser.error('This gal version requires reviewing its original native storage contract')
        # Reuse the upstream WinRT algorithms verbatim. Only desktop registrar
        # glue is excluded; the application and cached package remain untouched.
        source = original.read_text()
        helpers = source[source.index('namespace gal {'):source.index('void GalPlugin::RegisterWithRegistrar')]
        header = output / 'gal_storage_original.h'
        header.write_text('#pragma once\n#include <flutter/encodable_value.h>\n'
            '#include <flutter/method_result.h>\n#include <windows.h>\n'
            '#include <winrt/Windows.Foundation.h>\n#include <winrt/Windows.Storage.h>\n'
            '#include <winrt/Windows.Storage.Streams.h>\n#include <winrt/Windows.System.h>\n'
            '#include <algorithm>\n#include <optional>\n#include <unordered_map>\n'
            '#include <stdexcept>\n' + helpers + '\n}  // namespace gal\n')
        include += ['-I', str(output)]
    registrant = output / 'generated_uwp_plugin_registrant.cpp'
    registrant.write_text(''.join(
        '#include "dev/windows_arm32/plugins/' + directory + '/registration.h"\n'
        for directory, _ in selected) +
        '#include "flutter/shell/platform/windows/uwp/flutter_view.h"\n'
        '#include "flutter/shell/platform/windows/uwp/diagnostics.h"\n' +
        ('namespace flutter::winrt::plugins { void Diagnostic(const char* text, HRESULT hr) '
         '{ flutter::winrt::Diagnostic(text, hr); } }\n' if selected else '') +
        'void RegisterUwpPlugins(flutter::winrt::PlatformMessageHandlers& handlers) {\n' +
        ('  auto native_window_session = '
         'flutter::winrt::plugins::RegisterNativeWindowSession(handlers);\n' if native_window else '') +
        ''.join('  flutter::winrt::plugins::' + function + '(handlers' +
                (', native_window_session' if directory in native_window_adapters else '') + ');\n'
                for directory, function in selected) + '}\n')
    if args.config_only:
        print(registrant)
        return
    uwp = root / 'engine/src/flutter/shell/platform/windows/uwp'
    sources = [
        ('flutter_view', uwp / 'flutter_view.cc'),
        ('diagnostics', uwp / 'diagnostics.cc'),
        ('runner', root / 'dev/windows_arm32/runner/main.cpp'),
        ('generated_uwp_plugin_registrant', registrant),
        ('standard_codec', root / 'engine/src/flutter/shell/platform/common/client_wrapper/standard_codec.cc'),
        *sources,
    ]
    common = ['-D_WIN32_WINNT=0x0A00', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP',
              '-DFLUTTER_WINDOWS_PHONE', '-std=c++20', '-fexceptions', '-frtti', '-O2', '-Werror']
    for directory in (root, root / 'engine/src', root / 'engine/src/flutter/third_party/angle/include',
                      root / 'engine/src/flutter/third_party/rapidjson/include',
                      root / 'engine/src/flutter/shell/platform/common/client_wrapper/include'):
        common += ['-I', str(directory)]
    driver = root / 'dev/windows_arm32/toolchain/phone_driver.py'
    environment = dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(configuration))
    with (output / 'build.log').open('w') as log:
        def run(arguments):
            command = [sys.executable, str(driver), *arguments]
            log.write(json.dumps(command) + '\n')
            log.flush()
            subprocess.run(command, cwd=output, env=environment, stdout=log, stderr=log, check=True)
        for name, source in sources:
            run(['cxx', *common, *include, '-c', str(source), '-o', str(output / (name + '.obj'))])
        run(['link', '-fexceptions', '-frtti', '-municode', '-mwindows',
             *(str(output / (name + '.obj')) for name, _ in sources), '-luuid',
             '-o', str(output / 'flutter_uwp_runner.exe')])
    print(output / 'flutter_uwp_runner.exe')


if __name__ == '__main__':
    main()
