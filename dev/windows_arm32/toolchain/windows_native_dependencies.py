"""Audit private Windows x64 host tools without packaging Windows system DLLs."""
from pathlib import Path
import re
import shutil
import subprocess

# OS-provided imports are resolved on the user's Windows installation.
SYSTEM_DLLS = frozenset('''advapi32 bcrypt bcryptprimitives cabinet combase comctl32 comdlg32
crypt32 dbghelp dnsapi dwmapi gdi32 imm32 iphlpapi kernel32 mpr msvcrt ncrypt
netapi32 normaliz ntdll ole32 oleaut32 pathcch powrprof propsys psapi rpcrt4 secur32
setupapi shell32 shlwapi ucrtbase user32 userenv usp10 version winhttp wininet
winmm winspool wintrust ws2_32 wtsapi32'''.split())


def system_library(name):
    name = name.lower()
    return (name.endswith('.dll') and (name[:-4] in SYSTEM_DLLS or
            name.startswith(('api-ms-win-', 'ext-ms-win-'))))


def audit(path, libraries, llvm_readobj=None):
    reader = str(llvm_readobj or shutil.which('llvm-readobj') or '')
    if not reader:
        raise ValueError('Windows dependency auditing requires native llvm-readobj')
    if system_library(Path(path).name):
        raise ValueError('Windows system DLL must not be packaged: ' + str(path))
    output = subprocess.check_output([reader, '--coff-imports', str(path)], text=True)
    if 'Format: COFF-x86-64' not in output:
        raise ValueError('Expected Windows x64 host binary: ' + str(path))
    imports = re.findall(r'^\s+Name: (\S+)', output, re.M)
    private = {name.lower() for name in libraries}
    missing = sorted({name for name in imports
                      if not system_library(name) and name.lower() not in private})
    if missing:
        raise ValueError('Unpackaged Windows host dependencies in ' + str(path) + ': ' + ', '.join(missing))
    return imports
