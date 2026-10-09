#!/usr/bin/env python3
"""Run unchanged LocalSend builds with a disposable macOS SDK, then relocate it.

Use a checksum-verified CI archive. This runs on macOS, never in the Ubuntu
cross-build. It does not modify that archive or mark its CI metadata as native.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import select_host, verify_native_host

APPLICATION = '5ccc6dea192d1c697c2602bf456b2eb2ad8e9674'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def validate(args):
    host = select_host(args.host)
    if platform.system() != 'Darwin' or host['os'] != 'macos':
        raise ValueError('Run on macOS with the matching native SDK tools')
    work, sdk, relocated, application = (path.resolve() for path in (
        args.work, args.sdk, args.relocated_sdk, args.application))
    repository = Path(__file__).resolve().parents[3]
    if (sdk == repository or not sdk.is_relative_to(work) or sdk == work
            or not relocated.is_relative_to(work) or relocated.exists()
            or sdk.is_relative_to(relocated) or relocated.is_relative_to(sdk)
            or ' ' not in str(relocated) or not any(ord(c) > 127 for c in str(relocated))):
        raise ValueError('Use disposable SDK paths inside work; the new path needs spaces and Unicode')
    if not re.fullmatch(r'[0-9a-f]{64}', args.sdk_archive_sha256):
        raise ValueError('Supply the verified original CI archive checksum')
    output = args.output.resolve()
    if (output.exists() or not output.is_relative_to(work)
            or output.is_relative_to(sdk) or output.is_relative_to(relocated)
            or (sdk/'.git/objects/info/alternates').exists()):
        raise ValueError('Use a new report inside work and a self-contained disposable SDK')
    if shutil.disk_usage(work).free < (512 + 4 + 4 + 512) * 1024**2:
        raise ValueError('Retain the 512 MiB reserve plus room for both application builds')
    bootstrap = json.loads((sdk/'bin/cache/windows-arm32-host-bootstrap.json').read_text())
    if bootstrap['sdkHost'] != host['id']:
        raise ValueError('SDK archive host differs from native test host')
    architecture = 'arm64' if host['id'] == 'macos-arm64' else 'x86_64'
    prefix = ['/usr/bin/arch', '-arm64' if architecture == 'arm64' else '-x86_64']
    actual_architecture = subprocess.check_output(prefix + ['/usr/bin/uname', '-m'], text=True).strip()
    if actual_architecture != architecture:
        raise ValueError('Destination execution architecture differs')
    if subprocess.check_output(['git','-C',str(application),'rev-parse','HEAD'], text=True).strip() != APPLICATION:
        raise ValueError('Use the pinned original LocalSend checkout')
    for directory in (application, sdk):
        subprocess.run(['git','-C',str(directory),'diff','--exit-code'], check=True)
        subprocess.run(['git','-C',str(directory),'diff','--cached','--exit-code'], check=True)
    for path in sdk.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(sdk):
            raise ValueError('SDK link escapes its distribution')
    environment = dict(os.environ, CI='true', FLUTTER_SUPPRESS_ANALYTICS='true',
        DART_SUPPRESS_ANALYTICS='true', PYTHONDONTWRITEBYTECODE='1',
        TMPDIR=str(work/'tmp'), TMP=str(work/'tmp'), TEMP=str(work/'tmp'),
        CARGO_HOME=str(work/'cargo'))
    environment.setdefault('PUB_CACHE', str(work/'pub-cache'))
    (work/'tmp').mkdir(exist_ok=True)
    logs = work/'native-sdk-logs'; logs.mkdir(exist_ok=True)
    report = {'schemaVersion':1, 'sdkHost':host['id'],
        'sourceRevision':bootstrap['sourceRevision'], 'sdkArchiveSha256':args.sdk_archive_sha256,
        'executionSystem':'Darwin', 'executionArchitecture':actual_architecture,
        'applicationCommit':APPLICATION, 'builds':[]}

    def run(command, cwd, name):
        path = logs/(name+'.log')
        with path.open('x') as output:
            path.chmod(0o600)
            result = subprocess.run(command, cwd=cwd, env=environment,
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError('Native command failed; inspect '+str(path))
        return digest(path)

    project = application/'app'
    for phase, current in (('initial',sdk), ('relocated',relocated)):
        if phase == 'relocated':
            sdk.rename(relocated)
            if sdk.exists():
                raise ValueError('Original SDK remains reachable during the relocation test')
            report['originalSdkPathUnavailableAfterRelocation'] = True
        dependencies = current/'bin/cache/windows-arm32-dependencies'
        for binary in (current/'bin/cache/dart-sdk/bin/dart',
                       dependencies/'compiler/bin/clang',
                       dependencies/'python/bin/python3.14',
                       dependencies/'msix/bin/makemsix',
                       dependencies/'rust-1.95.0/bin/rustc',
                       dependencies/'rust-1.93.1/bin/rustc'):
            if not binary.resolve().is_relative_to(current):
                raise ValueError('Native host dependency escapes this SDK')
            verify_native_host(binary, host)
        flutter = prefix + ['/bin/bash', str(current/'bin/flutter')]
        run(flutter + ['pub','get','--enforce-lockfile'], project, phase+'-pub-get')
        run(prefix + [str(dependencies/'msix/bin/makemsix'), 'pack',
            '-d',str(application/'msix'), '-p',str(project/'windows/localsend_msix_helper.msix')],
            project, phase+'-msix-helper')
        for family in ('rt','phone'):
            command = ['flutter','build','windows','--release','--target-platform','windows-arm']
            if family == 'phone': command += ['--windows-family','phone']
            command += ['--no-pub']
            sha = run(flutter + command[1:], project, phase+'-'+family)
            report['builds'].append({'phase':phase,'family':family,
                'command':command,'exitCode':0,'logSha256':sha})
        for directory in (application,current):
            subprocess.run(['git','-C',str(directory),'diff','--exit-code'], check=True)
            subprocess.run(['git','-C',str(directory),'diff','--cached','--exit-code'], check=True)
    report.update(applicationSourcesUnmodified=True, sdkSourcesUnmodified=True,
                  nativeDependencyPathsPreflightPassed=True)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work','sdk','relocated-sdk','application','output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--host', choices=('macos-x64','macos-arm64'), required=True)
    parser.add_argument('--sdk-archive-sha256', required=True)
    validate(parser.parse_args())
