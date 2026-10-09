#!/usr/bin/env python3
"""Install source-built RT/Phone engine and AOT artifacts into an SDK cache."""
import argparse
import json
import os
from pathlib import Path
import shutil
import shlex

from build_engine import artifacts, digest


def install(output, build, llvm_bin, frontend, patched_sdk, host_cpu, require_contained_tools=False,
            families=('rt', 'phone'), python=None):
    root = Path(__file__).resolve().parents[2]
    output, build = output.resolve(), build.resolve()
    llvm_bin = llvm_bin.resolve()
    sdk_root = output.parents[3]
    tools_contained = llvm_bin.is_relative_to(sdk_root)
    if require_contained_tools and not tools_contained:
        raise ValueError('Distribution conversion tools must reside inside the SDK')
    if python is not None:
        python = Path(os.path.abspath(python))
        if not python.is_file() or not os.access(python, os.X_OK):
            raise ValueError('Missing executable snapshot Python interpreter')
        if require_contained_tools and (not python.is_relative_to(sdk_root) or
                not python.resolve().is_relative_to(sdk_root) or
                not python.parent.resolve().is_relative_to(sdk_root)):
            raise ValueError('Distribution snapshot interpreter must reside inside the SDK')
    files = {}
    if not families or any(family not in ('rt', 'phone') for family in families):
        raise ValueError('Select RT, Phone or both runtime families')
    # Validate the whole input set before changing the destination.
    for family in families:
        directory = build / 'out' / ('win_release_arm_' + family)
        artifacts(directory, family, host_cpu)
        names = (('flutter_windows.dll', 'flutter_windows.dll.lib', 'libEGL.dll',
                  'libGLESv2.dll', 'icudtl.dat') if family == 'rt' else
                 ('flutter_engine.dll', 'flutter_engine.dll.lib', 'libEGL.dll',
                  'libGLESv2.dll', 'flutter_winrt_compat.dll', 'icudtl.dat'))
        prefix = Path('windows-arm-release') / ('phone' if family == 'phone' else '')
        for name in names:
            files[prefix / name] = directory / name
    release = Path('windows-arm-release')
    raw = build / ('out/win_release_arm_' + families[0]) / ('clang_' + host_cpu) / 'gen_snapshot'
    files[release / 'gen_snapshot_raw'] = raw
    for name in ('gen_snapshot.py', 'elf_code_ranges.py',
                 'lower_a32_code_ranges_to_fixed_thumb2.py',
                 'patch_app_so_with_fixed_thumb2_text.py', 'check_fixed_thumb2_aot.py'):
        files[release / name] = root / 'dev/windows_arm32/aot' / name
    files[release / 'frontend_server_aot.dart.snapshot'] = frontend.resolve()
    files[release / 'frontend_server_starter.dart'] = root / 'dev/windows_arm32/aot/frontend_server_starter.dart'
    for name in ('platform_strong.dill', 'vm_outline_strong.dill'):
        files[release / 'flutter_patched_sdk' / name] = patched_sdk.resolve() / name
    shell = root / 'engine/src/flutter/shell/platform'
    for name in ('flutter_export.h', 'flutter_messenger.h', 'flutter_plugin_registrar.h',
                 'flutter_texture_registrar.h'):
        files[release / name] = shell / 'common/public' / name
    files[release / 'flutter_windows.h'] = shell / 'windows/public/flutter_windows.h'
    for directory in ('common/client_wrapper', 'windows/client_wrapper'):
        source = shell / directory
        for path in source.rglob('*'):
            if path.is_file() and path.suffix in ('.h', '.cc', '.gni'):
                relative = Path('windows-arm/cpp_client_wrapper') / path.relative_to(source)
                if relative in files and digest(files[relative]) != digest(path):
                    raise ValueError('Conflicting upstream wrapper file: ' + str(relative))
                files[relative] = path
    for path in files.values():
        if not path.is_file() or not path.stat().st_size:
            raise ValueError('Missing source artifact: ' + str(path))
    for name in ('llvm-objdump', 'llvm-mc', 'llvm-objcopy', 'llvm-readelf'):
        if not os.access(llvm_bin / name, os.X_OK):
            raise ValueError('Missing LLVM conversion tool: ' + name)
    records = {}
    for relative, source in files.items():
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.absolute() != destination.absolute():
            if (destination.is_symlink() or
                    (destination.exists() and source.samefile(destination))):
                destination.unlink()
            shutil.copy2(source, destination)
        records[str(relative)] = {'sha256': digest(destination), 'bytes': destination.stat().st_size}
    directory = output / release
    launcher = directory / 'gen_snapshot'
    header = '#!/bin/sh\n'
    interpreter = 'python3'
    if python is not None:
        header += 'snapshot_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        header += 'export PATH="$snapshot_dir"/' + shlex.quote(os.path.relpath(python.parent, directory)) + ':"$PATH"\n'
        interpreter = '"$snapshot_dir"/' + shlex.quote(os.path.relpath(python, directory))
    launcher.write_text(header + 'exec ' + interpreter + ' "$(dirname "$0")/gen_snapshot.py" "$@"\n')
    launcher.chmod(0o755)
    configuration = {'rawGenerator': 'gen_snapshot_raw', 'rawGeneratorSha256': digest(raw),
                     'llvmBin': os.path.relpath(llvm_bin, directory)}
    (directory / 'gen_snapshot_config.json').write_text(json.dumps(configuration, indent=2) + '\n')
    for name in ('gen_snapshot', 'gen_snapshot_config.json'):
        records[str(release / name)] = {'sha256': digest(directory / name), 'bytes': (directory / name).stat().st_size}
    report = {'files': records, 'runtimeFilesFromExplicitEngineBuild': True,
              'runtimeFamiliesInstalled': list(families),
              'systemDllsCopied': False, 'applicationFilesIncluded': False,
              'conversionToolsInsideSdk': tools_contained,
              'frontendAndPlatformInputsBuiltByThisCommand': False,
              'completeSdkDistributionVerified': False}
    (directory / 'arm32-sdk-artifacts.json').write_text(json.dumps(report, indent=2) + '\n')
    return directory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='SDK bin/cache/artifacts/engine directory')
    parser.add_argument('--engine-build', type=Path, required=True)
    parser.add_argument('--llvm-bin', type=Path, required=True, help='Conversion tools retained inside the distributed SDK')
    parser.add_argument('--frontend-server', type=Path, required=True, help='Matching host frontend AOT snapshot')
    parser.add_argument('--patched-sdk', type=Path, required=True, help='Matching generated Flutter platform dill directory')
    parser.add_argument('--host-cpu', choices=('arm64', 'x64'), required=True)
    parser.add_argument('--platform', choices=('rt', 'phone', 'both'), default='both',
                        help='Install the selected freshly built runtime family or both')
    parser.add_argument('--require-contained-tools', action='store_true',
                        help='Reject an external LLVM path when assembling a distribution')
    parser.add_argument('--python', type=Path, help='SDK-owned interpreter for snapshot conversion')
    args = parser.parse_args()
    print(install(args.output, args.engine_build, args.llvm_bin, args.frontend_server,
                  args.patched_sdk, args.host_cpu, args.require_contained_tools,
                  ('rt', 'phone') if args.platform == 'both' else (args.platform,), args.python))
