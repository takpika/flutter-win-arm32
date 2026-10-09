#!/usr/bin/env python3
"""Package separately built common LLVM resource tools and their private DLLs."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tarfile

from package_compiler import sha
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import select_host, verify_native_host
from windows_native_dependencies import audit, system_library


def package(source, dependencies, gnu, output):
    source, dependencies, gnu, output = (p.resolve() for p in (source, dependencies, gnu, output))
    if output.exists():
        raise ValueError('Resource tool staging directory already exists')
    host = select_host('windows-x64')
    record = json.loads((source / 'source-provenance.json').read_text())
    manifest = json.loads(Path(__file__).with_name('compiler-source-manifest.json').read_text())
    if (record['host'] != host['id'] or not record['sourceBuilt']
            or record['builtTools'] != ['llvm-rc', 'llvm-cvtres']
            or record['revision'] != manifest['upstreamRevision']
            or record['patches'] != [{'patch': p['patch'], 'sha256': p['patchSha256']}
                                     for p in manifest['patches']]):
        raise ValueError('Resource tools differ from the common LLVM source')
    dep_record = json.loads((dependencies / 'host-dependency-provenance.json').read_text())
    for name, expected in dep_record['files'].items():
        if sha(dependencies / name) != expected:
            raise ValueError('Private resource-tool dependency changed: ' + name)
    binary = output / 'bin'; binary.mkdir(parents=True)
    candidates = {}
    for directory in (source/'bin', dependencies/'bin', dependencies/'lib',
                      gnu/'x86_64-w64-mingw32/bin'):
        for path in directory.glob('*.dll'):
            candidates.setdefault(path.name.lower(), []).append(path)
    pending = [source/'bin/llvm-rc.exe', source/'bin/llvm-cvtres.exe']
    seen = set()
    imports = {}
    reader = gnu / 'bin/llvm-readobj'
    while pending:
        path = pending.pop()
        if path.name.lower() in seen:
            continue
        if system_library(path.name):
            raise ValueError('Resource tools must not contain an OS library')
        verify_native_host(path, host)
        found = audit(path, set(candidates), reader)
        shutil.copy2(path, binary / path.name)
        seen.add(path.name.lower()); imports[path.name] = found
        for name in found:
            if system_library(name) or name.lower() in seen:
                continue
            choices = candidates.get(name.lower(), [])
            if not choices or len({sha(p) for p in choices}) != 1:
                raise ValueError('Missing or ambiguous resource-tool DLL: ' + name)
            pending.append(choices[0])
    for path in binary.iterdir():
        audit(path, seen, reader)
    shutil.copy2(Path(__file__).with_name('LLVM-LICENSE.txt'), output)
    shutil.copytree(dependencies/'share/licenses', output/'licenses/dependencies')
    shutil.copy2(gnu/'LICENSE.TXT', output/'licenses/GNU-runtime-LICENSE.txt')
    report = {'host': host['id'], 'source': record, 'nativeDependencies': imports,
              'files': {p.relative_to(output).as_posix(): sha(p)
                        for p in output.rglob('*') if p.is_file()},
              'systemLibrariesCopied': False, 'nativeExecutionVerified': False}
    (output/'resource-tools-provenance.json').write_text(json.dumps(report, indent=2)+'\n')
    archive = output.with_name(output.name+'.tar.gz')
    with tarfile.open(archive, 'w:gz') as target:
        target.add(output, arcname='.')
    archive.with_name(archive.name+'.sha256').write_text(sha(archive)+'  '+archive.name+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'dependencies', 'gnu', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    package(args.source, args.dependencies, args.gnu, args.output)
