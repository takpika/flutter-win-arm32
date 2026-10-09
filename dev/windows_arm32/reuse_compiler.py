#!/usr/bin/env python3
"""Deploy a verified compiler from a successful run of this repository."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

from ci_cache import verify_compiler_archive


def validate_run(record, repository):
    if (record.get('status') != 'completed' or record.get('conclusion') != 'success' or
            record.get('event') not in ('workflow_dispatch', 'push') or
            record.get('head_repository', {}).get('full_name') != repository or
            record.get('path') not in ('.github/workflows/windows-arm32-engine.yml',
                                       '.github/workflows/windows-arm32-compiler.yml') or
            not re.fullmatch('[0-9a-f]{40}', record.get('head_sha', ''))):
        raise ValueError('Compiler input must be a successful trusted source-build run')
    return record['head_sha']


def producer_files():
    directory = Path('dev/windows_arm32/toolchain')
    manifest = json.loads((directory / 'compiler-source-manifest.json').read_text())
    return [directory / name for name in ('build_compiler.py', 'package_compiler.py',
            'compiler-source-manifest.json', 'LLVM-LICENSE.txt')] + [
                directory / item['patch'] for item in manifest['patches']]


def producer_step(text, name):
    lines = text.splitlines()
    start = lines.index('      - name: ' + name)
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith('      - ')), len(lines))
    step = lines[start:end]
    run = step.index('        run: |')
    return '\n'.join(step[run + 1:]).rstrip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    repository = os.environ['GITHUB_REPOSITORY']
    if not re.fullmatch('[0-9]+', args.run) or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        parser.error('Expected this repository and a numeric source-build run')
    work = args.work.resolve()
    if work.exists() or not work.is_relative_to(Path.cwd().resolve()):
        parser.error('Choose a new work directory inside this checkout')
    record = json.loads(subprocess.check_output(['gh', 'api',
        f'repos/{repository}/actions/runs/{args.run}'], text=True))
    revision = validate_run(record, repository)
    # Container checkout ownership can differ from the build user. Trust only
    # this checkout for these commands, without changing global Git policy.
    git = ['git', '-c', 'safe.directory=' + str(Path.cwd().resolve())]
    subprocess.run(git + ['fetch', '--no-tags', '--depth=1',
                    'https://github.com/' + repository + '.git', revision], check=True)
    sources = {}
    for path in producer_files():
        previous = subprocess.check_output(git + ['show', revision + ':' + path.as_posix()])
        if previous != path.read_bytes():
            raise ValueError('Compiler producer changed: ' + str(path))
        sources[path.as_posix()] = hashlib.sha256(previous).hexdigest()
    workflow = Path('.github/workflows/windows-arm32-compiler.yml')
    previous = subprocess.check_output(git + ['show', revision + ':' + workflow.as_posix()], text=True)
    for name in ('Build compiler and LLVM tools from pinned source', 'Package source-built tools and license'):
        if producer_step(previous, name) != producer_step(workflow.read_text(), name):
            raise ValueError('Compiler producer command changed: ' + name)
    work.mkdir(parents=True)
    inputs = work / 'input'
    subprocess.run(['gh', 'run', 'download', args.run, '--repo', repository,
                    '--name', 'windows-arm32-compiler-linux-x64', '--dir', str(inputs)], check=True)
    archive = inputs / 'windows-arm32-compiler-linux-x64.tar.gz'
    provenance = verify_compiler_archive(archive)
    destination = work.parent / 'compiler'
    if destination.exists():
        raise ValueError('Compiler destination already exists')
    with tarfile.open(archive) as source:
        source.extractall(destination, filter='data')
    for path in (archive, archive.with_name(archive.name + '.sha256')):
        shutil.copy2(path, work.parent / path.name)
    report = {'sourceRun': int(args.run), 'sourceRevision': revision,
              'compilerProducerFiles': sources, 'originalSourceProvenanceRetained': True,
              'compilerSha256': provenance['compilerSha256'],
              'archiveSha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
              'rebuiltWithCurrentHostPackages': False,
              'currentHostCompatibilityTestsRequired': True}
    (work.parent / 'compiler-reuse.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Verified source-built compiler artifact; current compiler/ABI tests remain required')


if __name__ == '__main__':
    main()
