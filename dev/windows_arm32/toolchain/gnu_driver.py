"""SDK-scoped GNU-ABI Windows ARM32 compiler and linker configuration."""
import json
import tempfile
import os
from pathlib import Path
import shlex
import subprocess
import sys

from msvc_options import translate
from windows_header_vfs import prepare as prepare_header_vfs


def expand(arguments):
    result = []
    for argument in arguments:
        if argument.startswith('@'):
            result.extend(expand(shlex.split(Path(argument[1:]).read_text())))
        else:
            result.append(argument)
    return result


def main(platform):
    if platform not in ('rt', 'phone'):
        raise ValueError('Unknown Windows ARM32 platform')
    mode, raw = sys.argv[1], sys.argv[2:]
    if mode not in ('cc', 'cxx', 'asm', 'ar', 'link', 'solink'):
        raise ValueError('Unknown compiler mode: ' + mode)
    config_path = Path(os.environ.get('FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG',
                                     Path(__file__).parent / 'toolchain.json')).resolve()
    config = json.loads(config_path.read_text())
    def configured(name):
        value = Path(config[name])
        return value if value.is_absolute() else (config_path.parent / value).resolve()
    sdk, llvm, compiler = configured('sysroot'), configured('llvmBin'), configured('compiler')
    scratch = Path.cwd() / '.windows-arm32-cache'
    (scratch / 'tmp').mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch / 'tmp'), TMP=str(scratch / 'tmp'),
               TEMP=str(scratch / 'tmp'), CLANG_MODULE_CACHE_PATH=str(scratch / 'modules'))
    if mode == 'ar':
        return subprocess.call([str(llvm / 'llvm-ar'), *raw], env=env)
    delayed = []
    if platform == 'rt':
        raw = expand(raw)
        delayed = [arg.split(':', 1)[1] for arg in raw if arg.startswith('/DELAYLOAD:')]
        raw = [arg for arg in raw if not arg.startswith('/DELAYLOAD:')]
    arguments = translate(raw)
    if mode == 'cxx' and any(value.split('=', 1)[0] ==
                            '-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING'
                            for value in arguments):
        arguments += ['-include', str(Path(__file__).with_name('msvc_codecvt_compat.h'))]
    # Track SDK system headers as well as project headers.
    arguments = ['-MD' if arg == '-MMD' else arg for arg in arguments]
    target = 'armv7-w64-mingw32' + ('uwp' if platform == 'phone' else '')
    resource = configured('resourceDir')
    if mode in ('link', 'solink') and config.get('runtimeResourceDir'):
        resource = configured('runtimeResourceDir')
    command = [str(compiler), '--target=' + target, '--sysroot=' + str(sdk),
               '-D_UCRT', '-D__STDC_LIMIT_MACROS', '-D__STDC_CONSTANT_MACROS',
               '-resource-dir=' + str(resource),
               '-fdeclspec', '-fms-extensions', '-fno-exceptions']
    header_roots = [sdk / 'generic-w64-mingw32/include',
                    sdk / 'armv7-w64-mingw32/include']
    for key, switch in (('includeDirs', '-I'), ('systemIncludeDirs', '-isystem')):
        for directory in config.get(key, []):
            include = Path(directory)
            if not include.is_absolute():
                include = (config_path.parent / include).resolve()
            command += [switch, str(include)]
            header_roots.append(include)
    if sys.platform.startswith('linux') and mode in ('cc', 'cxx', 'asm'):
        # Windows callers legitimately use different header filename casing.
        # Keep that compatibility in the cross toolchain, without renaming
        # shared SDK files or changing application/engine include directives.
        command += ['-ivfsoverlay', str(prepare_header_vfs(header_roots, scratch))]
    command += ['-isystem', str(Path(__file__).with_name('compat_include'))]
    # SwiftShader's older LLVM config was generated for MSVC. The GNU CRT
    # supplies unistd.h; select that existing header for this source group only.
    if any('llvm-subzero/build/Windows/include' in arg for arg in arguments):
        command += ['-DHAVE_UNISTD_H=1', '-DHAVE_INTTYPES_H=1',
                    '-DHAVE_LIBPSAPI=1', '-DHAVE_LIBSHELL32=1']
    if mode == 'cc':
        command += ['-x', 'c']
    if mode not in ('cc', 'asm'):
        command += ['-stdlib=libc++', '--driver-mode=g++']
    command += ['-fms-compatibility', '-fms-compatibility-version=19.44', '-fgnuc-version=4.2.1']
    if mode in ('link', 'solink') and platform == 'phone':
        # Mobile exposes job queries through the job API contract, rather than
        # a desktop kernel32.dll dependency. Keep the Dart implementation intact.
        command += ['-lwindowsapp', '-lucrtapp']
    command += arguments
    if mode == 'cc' and '-gcodeview' in arguments:
        # Preserve the existing ARM CodeView source-line workaround.
        command += ['-gline-tables-only']
    if mode in ('link', 'solink'):
        for directory in config.get(platform + 'LibraryDirs', config.get('libraryDirs', [])):
            library = Path(directory)
            if not library.is_absolute():
                library = (config_path.parent / library).resolve()
            command += ['-L' + str(library)]
        command += ['--ld-path=' + str(llvm / 'ld.lld'),
                    '-static', '-rtlib=compiler-rt', '-unwindlib=none']
        if platform == 'phone':
            command += ['-lunwind', '-Wl,--error-limit=0', '-Wl,--appcontainer',
                        '-Wl,--no-insert-timestamp', '-Wl,--major-subsystem-version,6',
                        '-Wl,--minor-subsystem-version,3', '-lwindowsapp', '-lucrtapp']
        else:
            command += ['-nostdlib++', '-lc++', '-lc++abi', '-lunwind', '-lwinpthread']
            command += ['-Wl,--delayload=' + name for name in delayed]
            if delayed:
                command.append('-ldelayimp')
    needs_guid_data = mode in ('link', 'solink') and '-luuid' in arguments
    if mode in ('link', 'solink') and (platform == 'phone' or needs_guid_data):
        # Generate only an import archive from the SDK contract declaration;
        # no operating-system binary is bundled or replaced. Mobile implements
        # process module enumeration in KernelBase (verified on the device).
        with tempfile.TemporaryDirectory(prefix='phone-imports-', dir=scratch / 'tmp') as temporary:
            archives = []
            for name in (('job', 'process') if platform == 'phone' else ()):
                archive = Path(temporary) / (name + '-api.a')
                subprocess.run([str(llvm / 'llvm-dlltool'), '-m', 'arm',
                                '-d', str(Path(__file__).with_name('phone_' + name + '_api.def')),
                                '-l', str(archive)], check=True, env=env)
                archives.append(str(archive))
            if needs_guid_data:
                guid_object = Path(temporary) / 'windows-guids.obj'
                subprocess.run([str(compiler), '--target=' + target,
                                '--sysroot=' + str(sdk), '-fdeclspec',
                                '-resource-dir=' + str(configured('resourceDir')),
                                '-c', str(Path(__file__).with_name('windows_guids.c')),
                                '-o', str(guid_object)], env=env, check=True)
                archives.append(str(guid_object))
            return subprocess.call([*command, *archives], env=env)
    result = subprocess.call(command, env=env)
    if result == 0 and mode in ('cc', 'cxx', 'asm') and '-MF' in arguments:
        depfile = Path(arguments[arguments.index('-MF') + 1])
        dependencies = [Path(__file__).resolve(),
                        Path(__file__).with_name(platform + '_driver.py').resolve(),
                        Path(__file__).with_name('msvc_options.py').resolve(),
                        Path(__file__).with_name('windows_header_vfs.py').resolve(),
                        config_path, compiler.resolve()]
        for directory in config.get('systemIncludeDirs', []):
            include = Path(directory)
            if not include.is_absolute():
                include = (config_path.parent / include).resolve()
            manifest = include.parent / 'sdk-provenance.json'
            if manifest.exists():
                dependencies.append(manifest)
        def escaped(path):
            return path.as_posix().replace('$', '$$').replace('#', '\\#').replace(' ', '\\ ').replace(':', '\\:')
        text = depfile.read_text().rstrip()
        depfile.write_text(text + ' ' + ' '.join(escaped(path) for path in dependencies) + '\n')
    return result
