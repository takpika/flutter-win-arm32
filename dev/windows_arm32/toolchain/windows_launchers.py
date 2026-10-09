"""Stage relocatable Windows launchers for SDK-owned Python tools."""
import os
from pathlib import Path
import shutil
import struct


def write_launcher(destination, binary, python, script, *, arguments=(), environment=None,
                   environment_paths=None):
    destination, binary, python, script = map(Path, (destination, binary, python, script))
    with binary.open('rb') as source:
        if source.read(2) != b'MZ':
            raise ValueError('Windows SDK launcher must be a native PE executable')
        source.seek(0x3c)
        source.seek(struct.unpack('<I', source.read(4))[0])
        if source.read(6) != b'PE\0\0\x64\x86':
            raise ValueError('Windows SDK launcher must target x64')
    relative_python = os.path.relpath(python, destination.parent)
    configuration = destination.parent / 'python-path.txt'
    content = relative_python + '\n'
    if configuration.exists() and configuration.read_text(encoding='utf-8') != content:
        raise ValueError('SDK launcher directory has a different Python interpreter')
    configuration.write_text(content, encoding='utf-8')
    stem = destination.with_suffix('') if destination.suffix == '.exe' else destination
    wrapper = Path(str(stem) + '.launcher.py')
    relative_script = os.path.relpath(script, destination.parent)
    argument_code = ', '.join(
        'str((directory / ' + repr(os.path.relpath(value, destination.parent)) + ').resolve())'
        if isinstance(value, Path) else repr(value) for value in arguments)
    # The interpreter executes this sidecar directly; user args never pass a shell.
    wrapper.write_text(
        'import os, runpy, sys\nfrom pathlib import Path\n'
        'directory = Path(__file__).resolve().parent\n'
        f'script = (directory / {relative_script!r}).resolve()\n'
        'sys.path.insert(0, str(script.parent))\n'
        'os.environ["PATH"] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get("PATH", "")\n'
        f'os.environ.update({dict(environment or {})!r})\n'
        f'for key, value in {dict(environment_paths or {})!r}.items():\n'
        '    os.environ[key] = str((directory / value).resolve())\n'
        f'sys.argv = [str(script), {argument_code + ", " if argument_code else ""}*sys.argv[1:]]\n'
        'runpy.run_path(str(script), run_name="__main__")\n', encoding='utf-8')
    shutil.copy2(binary, destination)
    return destination
