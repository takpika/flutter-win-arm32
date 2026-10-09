"""Translate explicitly supported MSVC options to the GNU Windows ARM driver."""
from pathlib import Path
import re
import shlex

MAP = {
    '/guard:cf': ['-mguard=cf'], '/GUARD:CF': ['-Wl,--guard-cf'],
    '/GS': ['-fstack-protector-strong'], '/GS-': ['-fno-stack-protector'],
    '/GR': ['-frtti'], '/GR-': ['-fno-rtti'], '/EHsc': ['-fexceptions'],
    '/O1': ['-Os'], '/O2': ['-O2'], '/Od': ['-O0'],
    '/Ob0': ['-fno-inline'], '/Ob2': ['-finline-functions'],
    '/Oy-': ['-fno-omit-frame-pointer'], '/Oy': ['-fomit-frame-pointer'],
    '/Gw': ['-fdata-sections'], '/Gy': ['-ffunction-sections'],
    '/Oi': ['-fbuiltin'], '/GF': ['-fmerge-all-constants'],
    '/WX': ['-Werror'], '/WX-': ['-Wno-error'],
    # MSVC does not diagnose unused private members at these levels. Keep
    # Clang-only diagnostics from becoming errors under the original /WX.
    '/W3': ['-Wall', '-Wno-unused-private-field', '-Wno-missing-braces'],
    '/W4': ['-Wall', '-Wextra', '-Wno-unused-private-field',
            '-Wno-missing-braces', '-Wno-missing-field-initializers'], '/W0': ['-w'],
    '/wd4005': ['-Wno-macro-redefined'],
    '/wd4100': ['-Wno-unused-parameter'],
    '/Z7': ['-gcodeview'], '/Zi': ['-gcodeview'],
    '/FS': [], '/MT': [], '/MTd': [], '/nologo': [],
    # ANGLE's MSVC error diagnostics mapped to Clang's corresponding groups.
    '/we4244': ['-Werror=implicit-int-conversion', '-Werror=implicit-float-conversion',
                '-Werror=float-conversion'],
    '/we4312': ['-Werror=int-to-pointer-cast'],
    '/we4456': ['-Werror=shadow'], '/we4458': ['-Werror=shadow'],
    '/we4715': ['-Werror=return-type'], '/we4800': ['-Werror=bool-conversion'],
    '/we4838': ['-Werror=c++11-narrowing'],
    '/we4596': ['-Werror=microsoft-extra-qualification'],
    '/we5264': ['-Werror=unused-const-variable'],
    '/we4855': ['-Werror=deprecated-this-capture'],
    '/bigobj': [], '/utf-8': ['-finput-charset=UTF-8'],
    '/OPT:REF': ['-Wl,--gc-sections'], '/OPT:ICF': ['-Wl,--icf=all'],
    '/DYNAMICBASE': ['-Wl,--dynamicbase'], '/NXCOMPAT': ['-Wl,--nxcompat'],
    '/DEBUG': ['-Wl,--pdb='],
    '/APPCONTAINER': ['-Wl,--appcontainer'],
    '/FIXED:NO': [], '/INCREMENTAL:NO': [], '/MACHINE:ARM': [],
}

def translate(arguments):
    result = []
    for arg in arguments:
        if arg.startswith('@'):
            result += translate(shlex.split(Path(arg[1:]).read_text()))
        elif arg in MAP:
            result += MAP[arg]
        elif re.fullmatch(r'/wd[0-9]+', arg):
            # Numeric MSVC warning IDs have no meaning to GNU-mode Clang.
            continue
        elif re.fullmatch(r'/D[A-Za-z_]\w*(?:=.*)?', arg):
            result.append('-D' + arg[2:])
        elif arg.startswith('/std:'):
            result.append('-std=' + arg[5:])
        elif arg.upper().startswith('/DEF:'):
            result.append(arg[5:])
        elif arg.startswith('/SUBSYSTEM:'):
            kind = arg.split(':',1)[1].split(',')[0]
            if kind not in ('WINDOWS', 'CONSOLE'):
                raise ValueError('Unsupported subsystem: ' + kind)
            result.append('-mwindows' if kind == 'WINDOWS' else '-mconsole')
        elif arg.lower().startswith('/ignore:') or arg.lower().startswith('/maxilksize:'):
            continue  # MSVC-only diagnostic/incremental-link bookkeeping.
        elif arg.startswith('/DELAYLOAD:'):
            raise ValueError('Phone does not permit desktop delayed imports: ' + arg)
        elif arg.startswith('--target='):
            continue  # Target is fixed below; GN's Windows config selects MSVC.
        elif arg.startswith('/') and '/' not in arg[1:] and not Path(arg).exists():
            raise ValueError('Unmapped MSVC option: ' + arg)
        elif arg.startswith('-l') and arg.endswith('.lib'):
            result.append('-l' + arg[2:-4].lower())
        elif not arg.startswith('-') and arg.endswith('.lib') and not Path(arg).exists():
            result.append('-l' + Path(arg).stem.lower())
        else:
            result.append(arg)
    profiles = [index for index, value in enumerate(arguments) if value in ('/W3', '/W4')]
    if profiles:
        # Some packages append GNU warning groups after apply_standard_settings.
        # Keep the requested MSVC profile for Clang-only style diagnostics, but
        # preserve any later explicit request for an individual diagnostic.
        later = arguments[profiles[-1] + 1:]
        for group in ('unused-private-field', 'missing-braces', 'missing-field-initializers',
                      'microsoft-extra-qualification', 'pragma-once-outside-header',
                      'unused-const-variable', 'unused-local-typedef', 'deprecated-this-capture',
                      'nonportable-include-path'):
            explicit = {'-W' + group, '-Wno-' + group,
                        '-Werror=' + group, '-Wno-error=' + group}
            if group == 'microsoft-extra-qualification':
                explicit.add('/we4596')
            if group == 'unused-const-variable':
                explicit.add('/we5264')
            if group == 'deprecated-this-capture':
                explicit.add('/we4855')
            if not any(value in explicit for value in later):
                result.append('-Wno-' + group)
    return result
