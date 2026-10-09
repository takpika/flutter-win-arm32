"""Restore the complete XPS thumbnail COM declaration from the pinned SDK."""
import re
import uuid


def guid(value):
    parsed = uuid.UUID(value)
    first, second, third = parsed.fields[:3]
    tail = ', '.join(f'0x{byte:02x}' for byte in parsed.bytes[8:])
    return f'{{0x{first:08x}, 0x{second:04x}, 0x{third:04x}, {{{tail}}}}}'


def generate(source, destination):
    text = source.read_text(encoding='utf-8-sig')
    begin = text.index('#ifndef __IXpsOMThumbnailGenerator_INTERFACE_DEFINED__')
    closing = re.search(r'#endif\s*/\*\s*__IXpsOMThumbnailGenerator_INTERFACE_DEFINED__\s*\*/',
                        text[begin:])
    end = begin + closing.end()
    block = text[begin:end]
    iid = re.search(r'MIDL_INTERFACE\("([0-9A-Fa-f-]+)"\)', block).group(1)
    clsid = re.search(r'class DECLSPEC_UUID\("([0-9A-Fa-f-]+)"\)\s*'
                      r'XpsOMThumbnailGenerator;', text).group(1)
    signature = re.search(r'virtual HRESULT STDMETHODCALLTYPE GenerateThumbnail\((.*?)\) = 0;',
                          block, re.S).group(1)
    if len(signature.split(',')) != 5:
        raise ValueError('Unexpected XPS thumbnail COM method layout')
    fields = uuid.UUID(iid)
    values = [f'0x{fields.time_low:08x}', f'0x{fields.time_mid:04x}',
              f'0x{fields.time_hi_version:04x}', *[f'0x{x:02x}' for x in fields.bytes[8:]]]
    destination.write_text(
        '#pragma once\n#include_next <XpsObjectModel.h>\n\n'
        '#ifndef DECLSPEC_XFGVIRT\n#if defined(_CONTROL_FLOW_GUARD_XFG)\n'
        '#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))\n'
        '#else\n#define DECLSPEC_XFGVIRT(base, func)\n#endif\n#endif\n\n'
        '// Complete C and C++ COM ABI copied from the verified Microsoft SDK.\n'
        + '#ifndef __IXpsOMThumbnailGenerator_INTERFACE_DEFINED__\n'
        + block + '\n\n#ifdef __CRT_UUID_DECL\n'
        + '__CRT_UUID_DECL(IXpsOMThumbnailGenerator, ' + ', '.join(values) + ')\n#endif\n'
        + 'EXTERN_C __declspec(selectany) const GUID IID_IXpsOMThumbnailGenerator = '
        + guid(iid) + ';\n#endif\n'
        + 'EXTERN_C __declspec(selectany) const GUID CLSID_XpsOMThumbnailGenerator = '
        + guid(clsid) + ';\n')
