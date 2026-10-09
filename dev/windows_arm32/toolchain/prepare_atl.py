#!/usr/bin/env python3
"""Build the original ATL runtime for the SDK's Windows ARM32 GNU C++ ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

VERSION = '14.29.30133'
PACKAGES = {
    'headers': ('3d39b68c61d0c298cd40d02f98b24633bb57637376530043789344f9bc342418',
        'https://download.visualstudio.microsoft.com/download/pr/8497e528-d106-4143-95eb-3deb1b2f4851/3d39b68c61d0c298cd40d02f98b24633bb57637376530043789344f9bc342418/Microsoft.VC.14.29.16.11.ATL.Headers.base.vsix'),
    'source': ('ead698ed4679db46a3f593746cbca74ca40f72fcc9a20d8de8bd69d11f6c690b',
        'https://download.visualstudio.microsoft.com/download/pr/8497e528-d106-4143-95eb-3deb1b2f4851/ead698ed4679db46a3f593746cbca74ca40f72fcc9a20d8de8bd69d11f6c690b/Microsoft.VC.14.29.16.11.ATL.Source.base.vsix'),
}


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def package(kind, supplied, output):
    expected, url = PACKAGES[kind]
    path = supplied.resolve() if supplied else output / 'downloads' / (kind + '.vsix')
    if not path.exists():
        if supplied:
            raise FileNotFoundError(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_suffix('.partial')
        try:
            with urllib.request.urlopen(url) as response, partial.open('wb') as stream:
                shutil.copyfileobj(response, stream)
            if sha(partial) != expected:
                raise ValueError('ATL package hash mismatch: ' + kind)
            partial.replace(path)
        finally:
            partial.unlink(missing_ok=True)
    if sha(path) != expected:
        raise ValueError('ATL package hash mismatch: ' + kind)
    return path


def extract(path, prefix, destination):
    hashes = {}
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            if not name.startswith(prefix) or not name.endswith(('.h', '.inl', '.cpp')):
                continue
            relative = Path(name[len(prefix):])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Invalid ATL source path')
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))
            hashes[str(relative)] = sha(target)
    if not hashes:
        raise ValueError('ATL package contains no expected source files')
    return hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-include', type=Path, required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--headers-package', type=Path)
    parser.add_argument('--source-package', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = 'Contents/VC/Tools/MSVC/' + VERSION + '/atlmfc/'
    header_hashes = extract(package('headers', args.headers_package, output),
                            prefix + 'include/', output / 'include')
    source_hashes = extract(package('source', args.source_package, output),
                            prefix + 'src/atl/atls/', output / 'source')
    original = args.toolchain_config.resolve()
    config = json.loads(original.read_text())
    for key in ('compiler', 'llvmBin', 'sysroot', 'resourceDir'):
        path = Path(config[key])
        config[key] = str(path if path.is_absolute() else (original.parent / path).resolve())
    for key in ('includeDirs', 'systemIncludeDirs'):
        config[key] = [str(Path(path).resolve() if Path(path).is_absolute()
                           else (original.parent / path).resolve())
                       for path in config.get(key, [])]
    config['systemIncludeDirs'] = list(dict.fromkeys([
        str(args.sdk_include.resolve()), str(output / 'include'),
        *config['systemIncludeDirs']]))
    # ATL's original sources include "stdafx.H" while the package stores
    # "stdafx.h". Expose this private SDK source directory to the Windows VFS.
    config['includeDirs'].append(str(output / 'source'))
    temporary_config = output / 'build-toolchain.json'
    temporary_config.write_text(json.dumps(config, indent=2) + '\n')
    # Parse the complete public declarations before the original library PCH
    # defines _ATL_STATIC_LIB_IMPL. Caller-visible classes remain available.
    preinclude = output / 'runtime-preinclude.h'
    preinclude.write_text('#define _ATL_DEBUG_INTERFACES\n#include <atlbase.h>\n#include <atlcom.h>\n')
    objects = output / 'objects'
    library = output / 'lib'
    objects.mkdir(exist_ok=True)
    library.mkdir(exist_ok=True)
    env = dict(os.environ, FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(temporary_config))
    port = Path(__file__).resolve().parent
    compiled = []
    for name in ('atlbase.cpp', 'atlfuncs.cpp', 'atldebuginterfacesmodule.cpp', 'stdafx.cpp'):
        obj = objects / (name + '.obj')
        command = [sys.executable, str(port / 'rt_driver.py'), 'cxx',
                   '-D_ATL_NO_DEFAULT_LIBS', '-DUNICODE', '-D_UNICODE',
                   '-D_WIN32_WINNT=0x0603', '-DNDEBUG', '-fexceptions',
                   '-fasync-exceptions', '-fdelayed-template-parsing',
                   '-include', str(preinclude), '-O1', '-c',
                   str(output / 'source' / name), '-o', str(obj)]
        subprocess.run(command, cwd=output, env=env, check=True)
        compiled.append(obj)
    archive = library / 'libatls.a'
    subprocess.run([str(Path(config['llvmBin']) / 'llvm-ar'), 'rcs', str(archive),
                    *map(str, compiled)], cwd=output, env=env, check=True)
    (output / 'source-provenance.json').write_text(json.dumps({
        'version': VERSION, 'packages': {kind: {'sha256': value[0], 'url': value[1]}
                                       for kind, value in PACKAGES.items()},
        'headerHashes': header_hashes, 'sourceHashes': source_hashes,
        'sourceModified': False, 'systemDllsCopied': False,
        'runtimeArchiveSha256': sha(archive),
        'runtimeObjectHashes': {obj.name: sha(obj) for obj in compiled},
        'compilerSha256': sha(Path(config['compiler'])),
    }, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
