#!/usr/bin/env python3
"""Execute SDK dependency configuration and assembly with native Windows Python."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(args):
    work, sdk = args.work.resolve(), args.sdk.resolve()
    output = work / 'windows-assembly-check'
    output.mkdir(exist_ok=False)
    temporary = output / 'tmp'
    temporary.mkdir()
    standard_input = output / 'empty-stdin'
    standard_input.write_bytes(b'')
    environment = dict(os.environ, WINEPREFIX=str(output / 'wine'), WINEDEBUG='-all',
                       TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary),
                       LANG='C.UTF-8', LC_ALL='C.UTF-8')
    for name in ('GH_TOKEN', 'GITHUB_TOKEN'):
        environment.pop(name, None)
    mapped_work = False
    def windows(file):
        if mapped_work and file.is_relative_to(work):
            return 'X:\\' + str(file.relative_to(work)).replace('/', '\\')
        return subprocess.check_output(['winepath', '-w', str(file)], env=environment,
                                       text=True, timeout=120).strip()
    if args.diagnose:
        # Keep crash debugging enabled, but collect its output without a GUI
        # prompt that would leave the native child and its parent waiting.
        key = r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug'
        for name, value in (('Debugger', r'C:\windows\system32\winedbg.exe --auto %ld %ld'),
                            ('Auto', '1')):
            result = subprocess.run(['wine', 'reg', 'add', key, '/v', name,
                                     '/t', 'REG_SZ', '/d', value, '/f'],
                                    env=environment, capture_output=True, text=True, timeout=120)
            (output / ('debugger-' + name + '.log')).write_text(result.stdout + result.stderr)
            result.check_returncode()
        environment['WINEDEBUG'] = '+seh,+loaddll'
    # Dart resolves Windows links in each path ancestor. Wine exposes the
    # container's /__w mount through Z: as a reparse point that cannot be read
    # with FSCTL_GET_REPARSE_POINT (4390). Map this CI work root directly so
    # native tools traverse actual SDK/project directories instead.
    windows(work)  # Initialize this isolated Wine prefix before adding a drive.
    drive = Path(environment['WINEPREFIX']) / 'dosdevices/x:'
    drive.symlink_to(work, target_is_directory=True)
    # Wine's mount manager was already initialized by winepath/reg above.
    # Reload only this isolated prefix after adding X:, so logical-drive
    # enumeration (and PowerShell's default FileSystem drives) sees the map.
    subprocess.run(['wineserver', '-k'], env=environment, check=True, timeout=60)
    subprocess.run(['wineserver', '-w'], env=environment, check=True, timeout=60)
    with (output / 'wineboot-stdout.log').open('w') as stdout, \
            (output / 'wineboot-stderr.log').open('w') as stderr:
        subprocess.run(['wineboot', '-u'], env=environment, check=True,
                       stdout=stdout, stderr=stderr, timeout=180)
    mapped_work = True
    dependencies = sdk / 'bin/cache/windows-arm32-dependencies'
    if args.diagnose:
        git = sdk / 'bin/mingit/mingw64/bin/git.exe'
        for name, options in (
                ('git-image', ['-p']),
                ('git-crash-code', ['-d', '--start-address=0x589400', '--stop-address=0x589540'])):
            result = subprocess.run(['x86_64-w64-mingw32-objdump', *options, str(git)],
                                    capture_output=True, text=True, timeout=120)
            (output / (name + '.log')).write_text(result.stdout + result.stderr)
            result.check_returncode()
    python = dependencies / 'python/python.exe'
    command = work / 'wine-cmd/cmd.exe'
    # Use the normal current Git for Windows prerequisite. Flutter's original
    # 2017 MinGit fallback remains unchanged in the SDK; its legacy MSYS shell
    # cannot run upload-pack in Wine 11 (see the preserved process trace).
    git = work / 'native-build-tools/git/cmd/git.exe'
    paths = [command.parent, work / 'wine-where', git.parent,
             work / 'native-build-tools/cmake-3.31.9-windows-x86_64/bin',
             work / 'native-build-tools/ninja', args.powershell.resolve().parent]
    environment.update(COMSPEC=windows(command), WINEPATH=';'.join(windows(p) for p in paths),
                       TMP=windows(temporary), TEMP=windows(temporary),
                       PUB_CACHE=windows(sdk / '.windows-arm32-cache/pub'),
                       PYTHONPYCACHEPREFIX=windows(sdk / '.windows-arm32-cache/python'),
                       PSModulePath=windows(args.powershell.resolve().parent / 'Modules'),
                       PYTHONUTF8='1', PYTHONIOENCODING='utf-8')
    invocation = output / 'native-invoke.py'
    invocation.write_text(
        'import faulthandler,runpy,sys\nfrom pathlib import Path\n'
        'faulthandler.enable()\nfaulthandler.dump_traceback_later(180, repeat=True)\n'
        'script=sys.argv.pop(1)\nsys.argv[0]=script\n'
        'sys.path.insert(0,str(Path(script).resolve().parent))\n'
        'runpy.run_path(script,run_name="__main__")\n')
    def native(stage, script, values, timeout=900):
        try:
            with (output / (stage+'-stdout.log')).open('w') as stdout, \
                    (output / (stage+'-stderr.log')).open('w') as stderr, \
                    standard_input.open('rb') as stdin:
                result = subprocess.run(['wine', str(python), windows(invocation), windows(script), *values],
                                        env=environment, stdin=stdin, stdout=stdout, stderr=stderr, timeout=timeout)
        except subprocess.TimeoutExpired:
            (output / (stage+'-process.json')).write_text(json.dumps({
                'timedOut':True,'timeoutSeconds':timeout},indent=2)+'\n')
            raise
        (output / (stage+'-process.json')).write_text(json.dumps({'exitCode':result.returncode},indent=2)+'\n')
        result.check_returncode()
    runtime = output / 'runtime-check.py'
    runtime.write_text("import sys,platform,json,PIL\nfrom pathlib import Path\n"
                       "assert sys.platform=='win32' and platform.machine().lower() in ('amd64','x86_64')\n"
                       "Path(sys.argv[1]).write_text(json.dumps({'platform':sys.platform,'python':sys.version,'pillow':PIL.__version__}))\n")
    native('python', runtime, [windows(output/'native-python.json')])
    if not (output/'native-python.json').is_file():
        raise ValueError('Native Windows Python did not execute the SDK runtime check')
    ps_script = output / 'powershell-commands.ps1'
    ps_script.write_bytes(b'\xef\xbb\xbf' + (
        "param([string]$Proof); $ErrorActionPreference='Stop'; try {\n"
        "$module=[IO.Path]::Combine($PSHOME,'Modules\\Microsoft.PowerShell.Management\\Microsoft.PowerShell.Management.psd1');\n"
        "[IO.File]::WriteAllLines($Proof+'.environment.txt',[string[]]@('PSHOME='+$PSHOME,'PSModulePath='+$env:PSModulePath,'AutoLoading='+$PSModuleAutoLoadingPreference,'ModuleManifestExists='+[IO.File]::Exists($module),'ManagementDllExists='+[IO.File]::Exists([IO.Path]::Combine($PSHOME,'Microsoft.PowerShell.Commands.Management.dll'))));\n"
        "$file=Join-Path $env:TEMP 'powershell-command-proof.txt';\n"
        "[IO.File]::WriteAllText($file,'native command proof'); Unblock-File -LiteralPath $file;\n"
        "$parent=Split-Path -Parent $file; $resolved=(Resolve-Path -LiteralPath $file).Path;\n"
        "if($parent -ne $env:TEMP -or $resolved -ne $file){throw 'Path commands returned different files'};\n"
        "$result=@{version=$PSVersionTable.PSVersion.ToString();managementCommandsExecuted=$true};\n"
        "[IO.File]::WriteAllText($Proof,($result | ConvertTo-Json));\n"
        "} catch { $failure=$_.Exception.ToString(); [IO.File]::WriteAllText($Proof+'.error.txt',$failure);\n"
        "try { Import-Module -Name $module -ErrorAction Stop; [IO.File]::WriteAllText($Proof+'.module-import.txt','Explicit manifest import succeeded; normal autoload remains unverified') }\n"
        "catch { [IO.File]::WriteAllText($Proof+'.module-import.txt',$_.Exception.ToString()) }; throw $failure };\n").encode('utf-8'))
    ps_runner = output / 'powershell-commands.py'
    ps_runner.write_text('import subprocess,sys\n'
                         'subprocess.run([sys.argv[1],"-NoLogo","-NoProfile","-NonInteractive",'
                         '"-File",sys.argv[2],sys.argv[3]],check=True,stdin=sys.stdin,stdout=sys.stdout,'
                         'stderr=sys.stderr,creationflags=subprocess.CREATE_NEW_CONSOLE)\n')
    for name in ('pwsh', 'powershell'):
        marker = output / (name + '-commands.json')
        native(name+'-commands', ps_runner,
               [windows(args.powershell.resolve()) if name == 'pwsh' else 'powershell',
                windows(ps_script), windows(marker)])
        command_proof = json.loads(marker.read_text()) if marker.is_file() else {}
        if name == 'pwsh' and not marker.is_file():
            # Compare with the managed launch used by the verified bootstrap.
            # Preserve the failed direct probe rather than assuming exit 0
            # means that Windows PowerShell executed its script.
            alternate = output / 'pwsh-start-commands.json'
            literal = lambda value: "'" + value.replace("'", "''") + "'"
            text = '& ' + literal(windows(ps_script)) + ' -Proof ' + literal(windows(alternate))
            with standard_input.open('rb') as stdin, \
                    (output / 'pwsh-start-stdout.log').open('w') as stdout, \
                    (output / 'pwsh-start-stderr.log').open('w') as stderr:
                result = subprocess.run(['wine', 'start', '/wait', '/unix', str(args.powershell.resolve()),
                    '-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand',
                    base64.b64encode(text.encode('utf-16le')).decode('ascii')],
                    env=environment, stdin=stdin, stdout=stdout, stderr=stderr, timeout=180)
            (output / 'pwsh-start-process.json').write_text(json.dumps({
                'exitCode':result.returncode, 'proofFileCreated':alternate.is_file()},indent=2)+'\n')
            # Retry the identical native child call after the diagnostic
            # managed launch. Require its own proof, not the alternate marker.
            native(name+'-commands-recheck', ps_runner,
                   [windows(args.powershell.resolve()), windows(ps_script), windows(marker)])
            command_proof = json.loads(marker.read_text()) if marker.is_file() else {}
            if not marker.is_file():
                variant = output / 'powershell-launch-variant.py'
                variant.write_text(
                    'import subprocess,sys\n'
                    'exe,script,marker,mode,encoded=sys.argv[1:]\n'
                    'args=[exe,"-NoLogo","-NoProfile","-NonInteractive"]\n'
                    'if mode=="encoded":args.extend(["-EncodedCommand",encoded])\n'
                    'else:args.extend(["-File",script,marker])\n'
                    'subprocess.run(args,check=True,creationflags=subprocess.CREATE_NEW_CONSOLE if mode=="console" else 0)\n')
                for mode in ('encoded', 'console'):
                    variant_marker = output / ('pwsh-'+mode+'-commands.json')
                    command = '& ' + literal(windows(ps_script)) + ' -Proof ' + literal(windows(variant_marker))
                    native('pwsh-'+mode, variant, [windows(args.powershell.resolve()), windows(ps_script),
                        windows(variant_marker), mode, base64.b64encode(command.encode('utf-16le')).decode('ascii')])
                # These are diagnostics, not substitutes for the consumer call.
                raise ValueError('Native default PowerShell launch remains unverified; argument/console variants recorded')
        if (command_proof.get('managementCommandsExecuted') is not True or
                command_proof.get('version') != '7.4.13'):
            raise ValueError('Native PowerShell management commands did not execute: '+name)
    git_probe = output / 'git-unicode-check.py'
    git_probe.write_text(
        'import subprocess,sys,json\nfrom pathlib import Path\n'
        'git,root=Path(sys.argv[1]),Path(sys.argv[2])\n'
        'source=root/"source 日本語";destination=root/"clone 日本語"\n'
        'source.mkdir()\n'
        'subprocess.run([str(git),"--version"],check=True)\n'
        'subprocess.run([str(git),"init",str(source)],check=True)\n'
        '(source/"日本語.txt").write_text("native Windows Git Unicode proof",encoding="utf-8")\n'
        'subprocess.run([str(git),"-C",str(source),"add","."],check=True)\n'
        'subprocess.run([str(git),"-C",str(source),"-c","user.name=SDK CI",'
        '"-c","user.email=sdk-ci@example.invalid","commit","-m","Unicode path proof"],check=True)\n'
        'subprocess.run([str(git),"clone","--no-local",str(source),str(destination)],check=True)\n'
        'assert (destination/"日本語.txt").read_text(encoding="utf-8")=="native Windows Git Unicode proof"\n'
        '(root/"git-unicode-verification.json").write_text(json.dumps({"nativeGitCloneVerified":True}))\n')
    previous_debug = environment['WINEDEBUG']
    environment.update(GIT_TRACE='1', WINEDEBUG='+process,+loaddll')
    try:
        native('git-unicode', git_probe, [windows(git), windows(output)])
    finally:
        environment.pop('GIT_TRACE', None)
        environment['WINEDEBUG'] = previous_debug
    configuration = dependencies / 'configuration'
    values = {
        'sdk':sdk, 'output':configuration, 'compiler':dependencies/'compiler',
        'gnu-sysroot':dependencies/'target-sysroot', 'windows-sdk':dependencies/'windows-sdk',
        'atl':dependencies/'atl', 'cppwinrt':dependencies/'cppwinrt', 'python':python,
        'packager':dependencies/'msix/bin/makemsix.exe',
        'rust-dependencies':dependencies/'rust-dependencies',
        'rust-stable':dependencies/'rust-1.95.0', 'rust-193':dependencies/'rust-1.93.1',
        'windows-launcher':dependencies/'launcher/sdk-python-launcher.exe',
    }
    arguments=[item for key,value in values.items() for item in ('--'+key,windows(value))]
    native('configure',sdk/'dev/windows_arm32/configure_sdk_dependencies.py',arguments,600 if args.diagnose else 1800)
    font_probe = output/'font-subset-check.py'
    font_probe.write_text(
        'import hashlib,json,shutil,subprocess,sys\nfrom pathlib import Path\n'
        'sdk,root=map(Path,sys.argv[1:])\n'
        'tool=sdk/"bin/cache/artifacts/engine/windows-x64/font-subset.exe"\n'
        'raw=tool.with_name("font-subset-raw.exe")\n'
        'font=sdk/"bin/cache/artifacts/material_fonts/MaterialIcons-Regular.otf"\n'
        'work=root/"font subset 日本語";work.mkdir()\n'
        'shutil.copyfile(font,work/"input.ttf")\n'
        'points=b"57490 62832 57706 57490 optional:1\\n"\n'
        'for name,command in (("reference",[str(raw),"reference.ttf","input.ttf"]),'
        '("wrapped",[str(tool),str(work/"出力 日本語.ttf"),str(font)])):\n'
        ' result=subprocess.run(command,cwd=work,input=points,capture_output=True)\n'
        ' (root/("font-subset-"+name+".log")).write_bytes(result.stdout+result.stderr)\n'
        ' if result.returncode: raise RuntimeError("Native font subset proof failed: "+name)\n'
        ' assert (work/"reference.ttf").read_bytes()==(work/"出力 日本語.ttf").read_bytes() if name=="wrapped" else True\n'
        '(root/"font-subset-verification.json").write_text(json.dumps({'
        '"unicodeInputAndOutputVerified":True,"originalBinaryPreserved":True,'
        '"optionalAndDuplicateCodepointsVerified":True,"subsetBytesMatchOriginal":True,'
        '"originalBinarySha256":hashlib.sha256(raw.read_bytes()).hexdigest(),'
        '"subsetBytes":(work/"reference.ttf").stat().st_size}))\n')
    native('font-subset',font_probe,[windows(sdk),windows(output)])
    native('native-arguments', sdk/'dev/windows_arm32/toolchain/check_native_arguments.py',
           [windows(configuration/'native-toolchain.json'), windows(output)])
    rust_host_probe = output / 'rust-host-check.py'
    rust_host_probe.write_text(
        'import subprocess,sys,json\nfrom pathlib import Path\n'
        'sdk,root=map(Path,sys.argv[1:])\n'
        'source=root/"host-unwind.rs"\n'
        'source.write_text(\'static DROPS:std::sync::atomic::AtomicUsize=std::sync::atomic::AtomicUsize::new(0);'
        'struct Guard;impl Drop for Guard{fn drop(&mut self){DROPS.fetch_add(1,std::sync::atomic::Ordering::SeqCst);}}'
        'fn main(){std::panic::set_hook(Box::new(|_|{}));'
        'assert!(std::panic::catch_unwind(||{let _guard=Guard;panic!("host unwind")}).is_err());'
        'assert_eq!(DROPS.load(std::sync::atomic::Ordering::SeqCst),1);'
        'let p=std::env::temp_dir().join("Rust 日本語.txt");std::fs::write(&p,"native host proof").unwrap();'
        'assert_eq!(std::fs::read_to_string(&p).unwrap(),"native host proof");std::fs::remove_file(p).unwrap();'
        'std::fs::write(std::env::args().nth(1).unwrap(),"RUST_HOST_PROOF").unwrap();}\',encoding="utf-8")\n'
        'for version in ("1.95.0","1.93.1"):\n'
        ' config=sdk/"bin/cache/windows-arm32-dependencies/configuration"\n'
        ' linker=config/("rust-targets-"+version)/"rust-host-link.exe"\n'
        ' compiler=sdk/"bin/cache/windows-arm32-dependencies"/("rust-"+version)/"bin/rustc.exe"\n'
        ' binary=root/("rust-host-"+version+".exe");marker=root/("rust-host-"+version+".txt")\n'
        ' subprocess.run([str(compiler),str(source),"-C","linker="+str(linker),"-C","linker-flavor=gcc","-C","link-self-contained=no",'
        '"--sysroot="+str(config/("rust-sysroot-"+version)),"-o",str(binary)],check=True)\n'
        ' subprocess.run([str(binary),str(marker)],check=True)\n'
        ' assert marker.read_text()=="RUST_HOST_PROOF"\n'
        '(root/"rust-host-verification.json").write_text(json.dumps({"versions":["1.95.0","1.93.1"],'
        '"nativeLinkAndExecutionVerified":True,"panicUnwindVerified":True,"unwindDestructorVerified":True,"unicodeFileIoVerified":True}))\n')
    native('rust-host', rust_host_probe, [windows(sdk), windows(output)])
    generator=work/'inputs/generator/build/gen_snapshot.exe'
    if not generator.is_file():
        raise ValueError('Expected original native snapshot generator artifact layout')
    values={
        'sdk':sdk, 'engine-build':work/'engines',
        'frontend-server':work/'inputs/frontend/frontend_server_aot.dart.snapshot',
        'patched-sdk':work/'engines/out/win_release_arm_rt/flutter_patched_sdk',
        'dependency-config':configuration/'sdk-dependencies.json',
        'host-generator':generator,
        'host-generator-provenance':work/'inputs/generator/snapshot-provenance.json',
    }
    arguments=[item for key,value in values.items() for item in ('--'+key,windows(value))]
    native('assemble',sdk/'dev/windows_arm32/assemble_sdk.py',arguments,1800)
    report=sdk/'bin/cache/artifacts/engine/windows-arm-release/sdk-assembly.json'
    proof=json.loads(report.read_text())
    if not proof['bothRuntimeFamiliesInstalled'] or not proof['sdkDependencyPathsPreflightPassed']:
        raise ValueError('Native SDK assembly did not pass dependency preflight')
    # Exercise the consumer flow after moving the complete SDK. No application
    # source patches or SDK reconfiguration are allowed after this move.
    sdk.rename(work / 'relocated Windows SDK 日本語')
    sdk = work / 'relocated Windows SDK 日本語'
    dependencies = sdk / 'bin/cache/windows-arm32-dependencies'
    python = dependencies / 'python/python.exe'
    environment.update(WINEDEBUG='-all',
                       FLUTTER_WINDOWS_ARM32_DIAGNOSTICS_DIRECTORY=windows(output / 'native-tool-failures'),
                       WINEPATH=';'.join(windows(p) for p in paths),
                       PUB_CACHE=windows(sdk / '.windows-arm32-cache/pub'),
                       PYTHONPYCACHEPREFIX=windows(sdk / '.windows-arm32-cache/python'),
                       CI='true', FLUTTER_SUPPRESS_ANALYTICS='true')
    application = work / 'localsend'
    build = output / 'application-build.py'
    build.write_text(
        'import subprocess,sys\nfrom pathlib import Path\n'
        'sdk,project=map(Path,sys.argv[1:3])\n'
        'subprocess.run([str(sdk/"bin/cache/windows-arm32-dependencies/msix/bin/makemsix.exe"),'
        '"pack","-d",str(project/"msix"),"-p",str(project/"app/windows/localsend_msix_helper.msix")],check=True)\n'
        'import os\n'
        # Wine initializes COMSPEC on native process startup. Select our full
        # source-built CI interpreter inside Windows, as in the bootstrap test.
        'os.environ["COMSPEC"]=sys.argv[3]\n'
        'for stage,arguments in (("pub","pub get --enforce-lockfile"),'
        '("rt","--verbose build windows --release --target-platform windows-arm --no-pub"),'
        '("phone","--verbose build windows --release --target-platform windows-arm --windows-family phone --no-pub")):\n'
        ' with (Path(sys.argv[4])/(stage+"-flutter-stdout.log")).open("w",encoding="utf-8") as out,'
        '(Path(sys.argv[4])/(stage+"-flutter-stderr.log")).open("w",encoding="utf-8") as err:\n'
        # Pass CMD's command line directly: Windows Python list quoting follows
        # the C runtime rules, which do not preserve CMD's embedded quotes.
        '  subprocess.run(\'"\'+os.environ["COMSPEC"]+\'" /d /c chcp 65001 >NUL & call "\'+str(sdk/"bin/flutter.bat")+\'" \'+arguments,'
        'executable=os.environ["COMSPEC"],cwd=project/"app",check=True,stdin=sys.stdin,stdout=out,stderr=err,'
        'creationflags=subprocess.CREATE_NEW_CONSOLE)\n'
        ' if stage=="pub":\n'
        '  import hashlib,json\n'
        '  plugins=json.loads((project/"app/.flutter-plugins-dependencies").read_text(encoding="utf-8"))["plugins"]["windows"]\n'
        '  selected=next(p for p in plugins if p["name"]=="permission_handler_windows")\n'
        '  source=Path(selected["path"])/"windows/permission_handler_windows_plugin.cpp"\n'
        '  body=source.read_bytes()\n'
        '  (Path(sys.argv[4])/"plugin-native-contract-source.json").write_text(json.dumps({'
        '"sourceFileExists":True,"sourceSha256":hashlib.sha256(body).hexdigest(),'
        '"crlfLineCount":body.count(b"\\r\\n"),"lfLineCount":body.count(b"\\n"),'
        '"packagePath":str(source)},ensure_ascii=True),encoding="utf-8")\n')
    native('application-build', build, [windows(sdk), windows(application), windows(command), windows(output)], 5400)
    git = ['git', '-c', 'safe.directory='+str(application), '-C', str(application)]
    subprocess.run([*git, 'diff', 'HEAD', '--exit-code'], check=True)
    commit = subprocess.check_output([*git, 'rev-parse', 'HEAD'], text=True).strip()
    if commit != '5ccc6dea192d1c697c2602bf456b2eb2ad8e9674':
        raise ValueError('Unexpected validation application revision')
    for artifact in ('app/build/windows/arm/runner/localsend_app.exe',
                     'app/build/windows/arm-phone/localsend_app.unsigned.appx'):
        if not (application / artifact).is_file():
            raise ValueError('Missing application output: ' + artifact)
    runtime_hashes = {}
    release = sdk / 'bin/cache/artifacts/engine/windows-arm-release'
    for family, directory, names in (
            ('rt', 'arm/runner', ('flutter_windows.dll', 'libEGL.dll', 'libGLESv2.dll')),
            ('phone', 'arm-phone/bundle', ('flutter_engine.dll', 'flutter_winrt_compat.dll',
                                         'libEGL.dll', 'libGLESv2.dll'))):
        runtime_hashes[family] = {}
        for name in names:
            source = release / ('phone' if family == 'phone' else '') / name
            bundled = application / 'app/build/windows' / directory / name
            expected = hashlib.sha256(source.read_bytes()).hexdigest()
            actual = hashlib.sha256(bundled.read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError('Application used a different SDK runtime: ' + family + '/' + name)
            runtime_hashes[family][name] = actual
    (output / 'application-runtime-hashes.json').write_text(json.dumps(runtime_hashes, indent=2)+'\n')
    report = sdk / 'bin/cache/artifacts/engine/windows-arm-release/sdk-assembly.json'
    proof.update(applicationBuildVerified=True, relocatedSdkBuildVerified=True,
                 validationApplicationCommit=commit, releaseReady=False)
    report.write_text(json.dumps(proof, indent=2)+'\n')
    (output/'assembly-verification.json').write_text(json.dumps({
        'sdkHost':'windows-x64','nativeWindowsPythonVerified':True,
        'bothRuntimeFamiliesAssembled':True,'applicationBuildVerified':True,
        'relocatedApplicationBuildVerified':True,
        'validationApplicationCommit':commit,'releaseReady':False},indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','work','powershell'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--diagnose',action='store_true',
                        help='Bound configuration diagnosis while recording Python stacks')
    run(parser.parse_args())
