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
        'firebase_core': ('firebase_core', 'RegisterFirebase'),
        'firebase_messaging': ('firebase_core', 'RegisterFirebase'),
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
            {'0360687dde3812ce74637bc550d404e15e3cea07bf52fc882c7f409672e57293',
             '8b0b5dc0236c19c1029ddcd339f7bae61f0adba3b940635f8b0d81b34882bcdd'}),
    }
    for name in set(args.plugin) & unimplemented.keys():
        if name not in packages:
            parser.error('Supply the original Windows package directory: ' + name)
        # These are exact LF and CRLF checkouts of the same pinned source.
        # Accept Git's Windows line endings without weakening the contract pin.
        relative, checksums = unimplemented[name]
        original = Path(packages[name]) / relative
        if not original.is_file():
            parser.error('Missing original native package source: ' + str(original))
        actual_checksum = hashlib.sha256(original.read_bytes()).hexdigest()
        if actual_checksum not in checksums:
            parser.error('This package has a different native contract; an adapter is required: ' +
                         name + ' (source SHA256 ' + actual_checksum + ')')
    unknown = set(args.plugin) - adapters.keys() - unimplemented.keys()
    if unknown:
        parser.error('Native UWP adapters are not installed for: ' + ', '.join(sorted(unknown)))
    selected = sorted({adapters[name] for name in args.plugin if name in adapters})
    native_window_adapters = {'bitsdojo_window_windows', 'tray_manager', 'windows_taskbar', 'window_manager'}
    native_window = any(directory in native_window_adapters for directory, _ in selected)
    include = []
    if {'open_dir_windows', 'pasteboard', 'gal', 'desktop_drop', 'firebase_messaging'} & set(args.plugin):
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
    core_include = []
    firebase = bool({'firebase_core', 'firebase_messaging'} & set(args.plugin))
    if firebase:
        if 'firebase_core' not in packages:
            parser.error('Supply the original firebase_core package directory')
        directory = Path(packages['firebase_core']).resolve() / 'windows'
        contract = {'messages.g.h': '45275bfb58aa57130b7e7ce24789ebe113a7e623feb3b0e634f781c7c051d6a9', 'messages.g.cpp': 'c0e4889cdaf408510181461415d2619df847c96b3b68341da42c25ddb5b748fb'}
        for name, checksum in contract.items():
            original = directory / name
            if not original.is_file() or hashlib.sha256(
                    original.read_bytes().replace(b'\r\n', b'\n')).hexdigest() != checksum:
                parser.error('This firebase_core version requires reviewing its generated contract: ' + name)
        # Published packages reuse messages.g.h and its include guard. Keep Core
        # in its own translation units, with its own package include path first.
        core_include = ['-I', str(directory)]
        sources += [
            ('firebase_core_messages', directory / 'messages.g.cpp'),
            ('firebase_core_native', root / 'dev/windows_arm32/plugins/firebase_core/registration.cpp'),
        ]
    sdk_include = []
    sdk_link = []
    if 'firebase_messaging' in args.plugin:
        from firebase_consumer import REQUIRED_ABI, load as load_firebase_consumer
        consumer, consumer_contract = load_firebase_consumer(root)
        sdk_include = ['-I', str(consumer / consumer_contract['headers']), '-I', str(output)]
        sdk_link = [str(consumer / consumer_contract['importLibrary'])]
        output.mkdir(parents=True, exist_ok=True)
        (output / 'firebase_consumer_config.h').write_text(
            '#pragma once\n#define FLUTTER_FIREBASE_MINIMUM_ABI ' +
            str(REQUIRED_ABI) + 'u\n' +
            '#define FLUTTER_FIREBASE_PROVISIONAL_REQUEST ' +
            str(consumer_contract['notificationSettings']['provisional']['requestedMask']) + 'u\n')
        sources.append(('firebase_messaging_notifications',
                        root / 'dev/windows_arm32/plugins/firebase_messaging/notifications.cpp'))
        sources.append(('firebase_messaging_activation',
                        root / 'dev/windows_arm32/plugins/firebase_messaging/activation.cpp'))
        sources.append(('headless_engine', root / 'dev/windows_arm32/runner/headless_engine.cpp'))
        sources.append(('firebase_messaging_background',
                        root / 'dev/windows_arm32/plugins/firebase_messaging/background.cpp'))
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
                for directory, function in selected) + '}\n' +
        'HRESULT RegisterUwpApplicationServices(std::shared_ptr<void>* lifetime) {\n' +
        ('  return flutter::winrt::plugins::RegisterFirebaseApplicationServices(lifetime);\n'
         if 'firebase_messaging' in args.plugin else
         '  lifetime->reset(); return S_OK;\n') + '}\n')
    if args.config_only:
        print(registrant)
        return
    uwp = root / 'engine/src/flutter/shell/platform/windows/uwp'
    # The common wrapper currently groups ReplyManager with the Win32 C ABI
    # messenger and texture implementations (upstream issue flutter/flutter#57146).
    # Use its exact, pinned ReplyManager implementation with EmbedderMessenger.
    wrapper = root / 'engine/src/flutter/shell/platform/common/client_wrapper/core_implementations.cc'
    reply_body = wrapper.read_text().split(
        '// ========== engine_method_result.h ==========', 1)[1].split(
        '// ========== method_channel.h ==========', 1)[0]
    if hashlib.sha256(reply_body.encode()).hexdigest() != 'b1642f60166a7045100d75175c13c27ffc193901ddd10acbbee5191a6d003bee':
        parser.error('The upstream ReplyManager contract changed; review the UWP wrapper build')
    reply_source = output / 'upstream_reply_manager.cc'
    reply_source.write_text(
        '// Copyright 2013 The Flutter Authors. All rights reserved.\n'
        '// BSD-style license: see the Flutter LICENSE file.\n'
        '#include <cassert>\n#include <iostream>\n#include <flutter/engine_method_result.h>\n'
        'namespace flutter {\n' + reply_body + '\n}\n')
    sources = [
        ('flutter_view', uwp / 'flutter_view.cc'),
        ('diagnostics', uwp / 'diagnostics.cc'),
        ('runner', root / 'dev/windows_arm32/runner/main.cpp'),
        ('generated_uwp_plugin_registrant', registrant),
        ('standard_codec', root / 'engine/src/flutter/shell/platform/common/client_wrapper/standard_codec.cc'),
        ('keyboard_reply_manager', reply_source),
        ('keyboard_handler', root / 'engine/src/flutter/shell/platform/windows/keyboard_key_handler.cc'),
        ('keyboard_embedder', root / 'engine/src/flutter/shell/platform/windows/keyboard_key_embedder_handler.cc'),
        ('keyboard_channel', root / 'engine/src/flutter/shell/platform/windows/keyboard_key_channel_handler.cc'),
        ('keyboard_map', root / 'engine/src/flutter/shell/platform/windows/flutter_key_map.g.cc'),
        ('keyboard_utils', root / 'engine/src/flutter/shell/platform/windows/keyboard_utils.cc'),
        ('keyboard_json_codec', root / 'engine/src/flutter/shell/platform/common/json_message_codec.cc'),
        ('fml_logging', root / 'engine/src/flutter/fml/logging.cc'),
        ('fml_log_settings', root / 'engine/src/flutter/fml/log_settings.cc'),
        ('fml_log_settings_state', root / 'engine/src/flutter/fml/log_settings_state.cc'),
        *sources,
    ]
    common = ['-D_WIN32_WINNT=0x0A00', '-DWINAPI_FAMILY=WINAPI_FAMILY_APP',
              '-DFLUTTER_WINDOWS_PHONE', '-std=c++20', '-fexceptions', '-frtti', '-O2', '-Werror']
    if 'firebase_messaging' in args.plugin:
        common += ['-DFLUTTER_FIREBASE_MESSAGING']
    # SDK-relative quoted includes must not shadow standard headers such as
    # <version> when Flutter's legacy version file exists in the SDK root.
    common += ['-iquote', str(root)]
    for directory in (root / 'engine/src', root / 'engine/src/flutter/third_party/angle/include',
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
            result = subprocess.run(command, cwd=output, env=environment, stdout=log, stderr=log)
            if result.returncode:
                log.flush()
                sys.stderr.write((output / 'build.log').read_text()[-16384:])
                result.check_returncode()
        for name, source in sources:
            per_source = core_include if name.startswith(('firebase_core_', 'firebase_messaging_')) else []
            if name == 'firebase_core_native' or name.startswith('firebase_messaging_'):
                per_source = [*per_source, *sdk_include]
            flags = list(common)
            mode = 'cxx'
            run([mode, *per_source, *flags, *include, '-c', str(source), '-o', str(output / (name + '.obj'))])
        run(['link', '-fexceptions', '-frtti', '-municode', '-mwindows',
             *(str(output / (name + '.obj')) for name, _ in sources), '-luuid',
             *sdk_link,
             '-o', str(output / 'flutter_uwp_runner.exe')])
    print(output / 'flutter_uwp_runner.exe')


if __name__ == '__main__':
    main()
