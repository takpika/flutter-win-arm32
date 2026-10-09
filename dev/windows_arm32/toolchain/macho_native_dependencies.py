"""Audit cross-packaged Mach-O payloads using an ordinary native LLVM tool."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sdk_hosts import verify_native_host


def audit(path, libraries, host, llvm_objdump):
    verify_native_host(path, host)
    architecture = 'x86_64' if host['cpu'] == 'x64' else 'arm64'
    output = subprocess.check_output([str(llvm_objdump), '--macho', '--dylibs-used',
                                      '--arch=' + architecture, str(path)], text=True)
    imports = [line.strip().split(' (compatibility version', 1)[0]
               for line in output.splitlines() if line.startswith('\t')]
    if Path(path).suffix == '.dylib':
        imports = imports[1:]  # Mach-O dylib identity precedes its dependencies.
    for name in imports:
        if name.startswith(('/usr/lib/', '/System/Library/')):
            continue
        if name.startswith(('@rpath/', '@loader_path/', '@executable_path/')) and Path(name).name in libraries:
            continue
        raise ValueError('Unpackaged Mach-O host dependency: ' + name)
    return imports
