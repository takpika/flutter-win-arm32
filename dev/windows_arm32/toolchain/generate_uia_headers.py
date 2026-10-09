"""Add missing native UI Automation declarations from the pinned Microsoft SDK."""
import re


def generate(core, propvarutil, output):
    text = core.read_text(encoding='utf-8-sig')
    declarations = []
    for name in ('BulletStyle', 'HorizontalTextAlignment', 'FlowDirections',
                 'TextDecorationLineStyle'):
        match = re.search(r'\benum\s+' + name + r'\s*\{[^}]*\}\s*;', text)
        if not match:
            raise ValueError('Missing SDK enum: ' + name)
        declarations.append(match.group(0))
    scroll = re.search(r'const double UIA_ScrollPatternNoScroll\s*=\s*-1\s*;', text)
    flags = 'DEFINE_ENUM_FLAG_OPERATORS(ProviderOptions)'
    if not scroll or flags not in text:
        raise ValueError('Missing SDK UIA scroll constant or enum flag operators')
    declarations += [scroll.group(0), flags]
    (output / 'uiautomation.h').write_text(
        '#pragma once\n#include_next <uiautomation.h>\n'
        '#if defined(__MINGW32__)\n'
        '// Native declarations copied verbatim from Microsoft SDK UIAutomationCore.h.\n'
        + '\n\n'.join(declarations) + '\n#endif\n')
    text = propvarutil.read_text(encoding='utf-8-sig')
    comparison = re.search(r'PSSTDAPI_\(int\) VariantCompare\([^;]*;', text)
    if not comparison:
        raise ValueError('Missing SDK VariantCompare declaration')
    (output / 'propvarutil.h').write_text(
        '#pragma once\n#include_next <propvarutil.h>\n'
        '#if defined(__MINGW32__) && WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)\n'
        + comparison.group(0) + '\n#endif\n')
