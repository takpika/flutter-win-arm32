#!/usr/bin/env python3
"""Run the normal Windows Flutter bootstrap before and after SDK relocation."""
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess

from sdk_hosts import select_host, verify_native_host


def verify(sdk, powershell, output, trace_bootstrap=False):
    sdk, powershell, output = sdk.resolve(), powershell.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    temporary = output / 'tmp'
    temporary.mkdir()
    environment = dict(os.environ, WINEPREFIX=str(output / 'wine'), WINEDEBUG='-all',
                       TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary))
    host = select_host('windows-x64')
    verify_native_host(powershell, host)
    expected = json.loads((sdk / 'bin/cache/windows-arm32-host-bootstrap.json').read_text())

    def windows(path):
        return subprocess.check_output(['winepath', '-w', str(path)], env=environment,
                                        text=True, timeout=120).strip()

    def literal(value):
        return "'" + value.replace("'", "''") + "'"

    # Capture a native file side effect too: a successful process status alone
    # does not prove that Wine executed the managed PowerShell command.
    version_file = output / 'powershell-version.txt'
    command = '[IO.File]::WriteAllText(' + literal(windows(version_file)) + ", $PSVersionTable.PSVersion.ToString()); $PSVersionTable.PSVersion.ToString()"
    runtime = subprocess.run(['wine', 'start', '/wait', '/unix', str(powershell), '-NoLogo', '-NoProfile',
                              '-NonInteractive', '-EncodedCommand',
                              base64.b64encode(command.encode('utf-16le')).decode('ascii')],
                             env=dict(environment, WINEDEBUG='+loaddll,+seh'),
                             capture_output=True, text=True, timeout=180)
    file_version = version_file.read_text(encoding='utf-8-sig') if version_file.is_file() else None
    (output / 'powershell-process.json').write_text(json.dumps({
        'exitCode': runtime.returncode, 'stdout': runtime.stdout,
        'stderr': runtime.stderr, 'nativeFileVersion': file_version}, indent=2) + '\n')
    runtime.check_returncode()
    if file_version != '7.4.13':
        raise ValueError('Native PowerShell did not execute its version-file command')

    # Select the upstream utility on the CI process PATH, outside the SDK payload.
    where = Path('_work/wine-where/where.exe').resolve()
    verify_native_host(where, host)
    cmd = Path('_work/wine-cmd/cmd.exe').resolve()
    verify_native_host(cmd, host)
    environment['COMSPEC'] = windows(cmd)
    probes = []
    for pattern, expected_code in (('git', 0), ('flutter_port_missing_91c78d', 1)):
        probe = subprocess.run(['wine', str(cmd), '/d', '/c', 'where /Q ' + pattern],
                               env=dict(environment, WINEPATH=windows(cmd.parent) + ';' + windows(where.parent) + ';' + windows(sdk / 'bin/mingit/cmd')),
                               capture_output=True, text=True, timeout=120)
        probes.append({'pattern': pattern, 'exitCode': probe.returncode,
                       'stdout': probe.stdout, 'stderr': probe.stderr})
        (output / 'wine-where-verification.json').write_text(json.dumps(probes, indent=2) + '\n')
        if probe.returncode != expected_code or probe.stdout or probe.stderr:
            trace = subprocess.run(['wine', str(cmd), '/d', '/c', 'where git'],
                                   env=dict(environment, WINEPATH=windows(cmd.parent) + ';' + windows(where.parent) + ';' + windows(sdk / 'bin/mingit/cmd'),
                                            WINEDEBUG='+loaddll,+module,+cmd,+where'),
                                   capture_output=True, text=True, timeout=120)
            (output / 'wine-where-trace-stdout.log').write_text(trace.stdout)
            (output / 'wine-where-trace-stderr.log').write_text(trace.stderr)
            raise ValueError('Upstream Wine WHERE /Q did not preserve real search semantics')

    # Exercise the stock Flutter handle layout, real kernel sharing conflicts,
    # and restoration after the redirected subroutine returns.
    lock = output / 'handle-test.lock'
    lock_probe = cmd.with_name('check-lock.exe')
    verify_native_host(lock_probe, host)
    held, released = output / 'held.txt', output / 'released.txt'
    fixture = output / 'handle-test.cmd'
    fixture.write_text('@echo off\n2>NUL (\n'
                       'REM stderr is saved as command handle 3.\n'
                       'call :held 2>&3 9>"' + windows(lock) + '"\n)\n'
                       'if errorlevel 1 exit /b 1\n'
                       'echo restored 1>&2\n'
                       '"' + windows(lock_probe) + '" "' + windows(lock) + '" released "' + windows(released) + '"\n'
                       'exit /b %errorlevel%\n:held\necho forwarded 1>&2\n'
                       '"' + windows(lock_probe) + '" "' + windows(lock) + '" held "' + windows(held) + '"\n'
                       'exit /b %errorlevel%\n', encoding='utf-8', newline='\r\n')
    result = subprocess.run(['wine', str(cmd), '/d', '/c', windows(fixture)],
                            env=environment, capture_output=True, text=True, timeout=120)
    proof = {'exitCode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
             'lockHeld': held.is_file() and held.read_text() == 'held',
             'lockReleased': released.is_file() and released.read_text() == 'released'}
    (output / 'wine-handles-verification.json').write_text(json.dumps(proof, indent=2) + '\n')
    if (result.returncode or not proof['lockHeld'] or not proof['lockReleased'] or
            'forwarded' not in result.stderr or 'restored' not in result.stderr):
        trace = subprocess.run(['wine', str(cmd), '/d', '/c', windows(fixture)],
                               env=dict(environment, WINEDEBUG='+cmd'),
                               capture_output=True, text=True, timeout=30)
        (output / 'wine-handles-trace-stdout.log').write_text(trace.stdout)
        (output / 'wine-handles-trace-stderr.log').write_text(trace.stderr)
        raise ValueError('Command stderr forwarding, exclusive lock or restoration failed')

    # A called batch can terminate by chaining to another batch, as Flutter
    # does after dart.exe. The internal stop sentinel must not become its status.
    child, caller = output / 'exit-child.cmd', output / 'exit-caller.cmd'
    child.write_text('@echo off\nexit /b %1\n', newline='\r\n')
    caller.write_text('@echo off\n"' + windows(child) + '" %1\necho unexpected\n',
                      newline='\r\n')
    statuses = []
    for expected_code in (0, 7):
        result = subprocess.run(['wine', str(cmd), '/d', '/c',
                                 'call ' + windows(caller) + ' ' + str(expected_code)],
                                env=environment, capture_output=True, text=True, timeout=30)
        statuses.append({'expected': expected_code, 'actual': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
        (output / 'wine-exit-status-verification.json').write_text(json.dumps(statuses, indent=2) + '\n')
        if result.returncode != expected_code or 'unexpected' in result.stdout:
            raise ValueError('Command interpreter did not preserve actual chained batch status')

    versions = {}
    for stage in ('original', 'relocated'):
        if stage == 'relocated':
            moved = sdk.with_name('relocated Windows SDK 日本語')
            sdk.rename(moved)
            sdk = moved
        script = output / (stage + '.ps1')
        native_stdout = output / (stage + '-native-stdout.log')
        native_stderr = output / (stage + '-native-stderr.log')
        text = "$ErrorActionPreference='Stop'; $sdk=" + literal(windows(sdk)) + ';\n'
        text += '$env:CI="true"; $env:FLUTTER_SUPPRESS_ANALYTICS="true";\n'
        text += '$env:COMSPEC=' + literal(windows(cmd)) + ';\n'
        text += '$env:PATH=' + literal(windows(cmd.parent)) + '+";"+' + literal(windows(where.parent)) + '+";"+(Join-Path $sdk "bin/mingit/cmd")+";"+'
        text += literal(windows(powershell.parent)) + '+";"+$env:PATH;\n'
        text += '$env:PUB_CACHE=Join-Path $sdk ".windows-arm32-cache/pub";\n'
        text += '$env:TEMP=' + literal(windows(temporary)) + '; $env:TMP=$env:TEMP;\n'
        text += "try { & $env:COMSPEC /d /c ('call \"'+(Join-Path $sdk \"bin/flutter.bat\")+'\" --version --machine') 1>"
        text += literal(windows(native_stdout)) + ' 2>' + literal(windows(native_stderr))
        text += '; exit $LASTEXITCODE } catch { [IO.File]::WriteAllText('
        text += literal(windows(native_stderr)) + ', $_.Exception.ToString()); exit 1 };\n'
        script.write_bytes(b'\xef\xbb\xbf' + text.encode('utf-8'))
        try:
            result = subprocess.run(['wine', 'start', '/wait', '/unix', str(powershell), '-NoLogo', '-NoProfile',
                                     '-NonInteractive', '-File', windows(script)],
                                    env=dict(environment, WINEDEBUG='+cmd' if trace_bootstrap else '-all'),
                                    capture_output=True, text=True,
                                    timeout=90 if trace_bootstrap else 1800)
        except subprocess.TimeoutExpired as error:
            for name, value in (('stdout', error.stdout), ('stderr', error.stderr)):
                data = value or b''
                if isinstance(data, bytes):
                    data = data.decode('utf-8', errors='replace')
                (output / (stage + '-' + name + '.log')).write_text(data[-2_000_000:])
            (output / (stage + '-process.json')).write_text(json.dumps({
                'stage': stage, 'timedOut': True, 'timeoutSeconds': error.timeout,
                'traceBootstrap': trace_bootstrap,
                'sourceRevision': expected['sourceRevision']}, indent=2) + '\n')
            raise
        (output / (stage + '-stdout.log')).write_text(result.stdout)
        (output / (stage + '-stderr.log')).write_text(result.stderr)
        (output / (stage + '-process.json')).write_text(json.dumps({
            'stage': stage, 'exitCode': result.returncode,
            'stdoutBytes': len(result.stdout.encode('utf-8')),
            'stderrBytes': len(result.stderr.encode('utf-8')),
            'sourceRevision': expected['sourceRevision']}, indent=2) + '\n')
        result.check_returncode()
        if not native_stdout.is_file():
            raise ValueError('Native Flutter did not produce its expected output file')
        native_output = native_stdout.read_text(encoding='utf-8-sig')
        # The first normal bootstrap runs pub, which writes progress before
        # Flutter's final machine JSON. Keep the full native output as evidence.
        json_start = native_output.rfind('\n{')
        info = json.loads(native_output[json_start + 1:] if json_start >= 0 else native_output)
        if info['frameworkRevision'] != expected['sourceRevision'] or not info['dartSdkVersion'].startswith('3.10.9'):
            raise ValueError('Normal Flutter bootstrap reported another SDK revision/version')
        versions[stage] = info
    report = {'sdkHost': host['id'], 'sourceRevision': expected['sourceRevision'],
              'nativeWindowsFlutterBatBootstrapVerified': True,
              'unicodeSdkRelocationBootstrapVerified': True,
              'verificationRuntime': 'Wine11 on Ubuntu amd64, official PowerShell 7.4.13 LTS',
              'versions': versions, 'ordinaryFlutterBuildVerified': False,
              'fullSdkDependenciesAssembled': False, 'releaseReady': False}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--powershell', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--trace-bootstrap', action='store_true',
                        help='Capture a bounded Wine cmd trace for startup diagnosis')
    args = parser.parse_args()
    verify(args.sdk, args.powershell, args.output, args.trace_bootstrap)
