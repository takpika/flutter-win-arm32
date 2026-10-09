#!/usr/bin/env python3
"""Project complete XAML interface ABI declarations for ANGLE's WRL consumer."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def generate(projection, sdk_xaml, sdk_controls, dxinterop, output):
    output.mkdir(parents=True, exist_ok=True)
    xaml = (projection / 'impl/Windows.UI.Xaml.0.h').read_text()
    controls = (projection / 'impl/Windows.UI.Xaml.Controls.0.h').read_text()
    sdk = sdk_xaml.read_text()
    records = []

    def interface(text, namespace, name, sdk_text, sdk_name=None):
        qualified = namespace + '::' + name
        pattern = (r'template <> struct abi<winrt::' + re.escape(qualified)
                   + r'>\s*\{\s*struct (?:WINRT_IMPL_NOVTABLE|__declspec\(novtable\)) type : (\w+)\s*\{(.*?)\n\s*\};')
        match = re.search(pattern, text, re.S)
        if not match:
            raise ValueError('Missing complete C++/WinRT ABI: ' + qualified)
        guid = re.search(r'guid_v<winrt::' + re.escape(qualified)
                         + r'>.*?; // ([0-9A-F-]{36})', text).group(1)
        native_name = sdk_name or name
        original = re.search(r'MIDL_INTERFACE\("([0-9A-Fa-f-]{36})"\)\s*'
                             + native_name + r' : public \w+\s*\{(.*?)\n\s*\};', sdk_text, re.S)
        if not original or original[1].lower() != guid.lower():
            raise ValueError('SDK interface identity differs: ' + native_name)
        methods = re.findall(r'__stdcall (\w+)\(', match[2])
        sdk_methods = re.findall('STD' + r'METHODCALLTYPE\s+(\w+)\s*\(', original[2])
        if methods != sdk_methods:
            raise ValueError('Complete SDK vtable differs: ' + native_name)
        body = match[2].replace('int32_t __stdcall', 'HRESULT STDMETHODCALLTYPE').replace(' noexcept', '')
        body = body.replace('winrt::event_token', 'EventRegistrationToken')
        body = body.replace('winrt::Windows::Foundation::Size', 'ABI::Windows::Foundation::Size')
        body = body.replace('winrt::Windows::Foundation::Point', 'ABI::Windows::Foundation::Point')
        body = body.replace('winrt::Windows::Foundation::Rect', 'ABI::Windows::Foundation::Rect')
        body = body.replace('struct struct_Windows_UI_Xaml_Thickness', 'Thickness')
        body = body.replace('get_Dispatcher(void**)', 'get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher**)')
        if name == 'SizeChangedEventHandler':
            body = body.replace('Invoke(void*, void*)', 'Invoke(IInspectable*, ISizeChangedEventArgs*)')
        base = 'IUnknown' if match[1] == 'unknown_abi' else 'IInspectable'
        records.append({'interface': native_name, 'iid': guid, 'completeMethods': methods,
                        'methodCount': len(methods), 'matchedOriginalSdkVtable': True})
        return 'MIDL_INTERFACE("' + guid + '") ' + native_name + ' : public ' + base + ' {\n' + body + '\n};\n'

    parts = ['#pragma once\n#include <windows.ui.core.h>\n#include <cstdint>\n',
             'namespace ABI::Windows::UI::Xaml {\nstruct ISizeChangedEventArgs;\n']
    thickness = re.search(r'struct Thickness\s*\{(.*?)\};', sdk, re.S)
    if not thickness:
        raise ValueError('SDK Thickness layout missing')
    parts.append('struct Thickness {' + thickness[1] + '};\n')
    for name in ('IDependencyObject', 'IFrameworkElement', 'IUIElement', 'ISizeChangedEventArgs', 'SizeChangedEventHandler'):
        parts.append(interface(xaml, 'Windows::UI::Xaml', name, sdk,
                               'ISizeChangedEventHandler' if name == 'SizeChangedEventHandler' else None))
    parts.append('static_assert(sizeof(Thickness) == 32);\n}\n')
    parts.append('namespace ABI::Windows::UI::Xaml::Controls {\n')
    parts.append(interface(controls, 'Windows::UI::Xaml::Controls', 'ISwapChainPanel', sdk_controls.read_text()))
    parts.append('}\n')
    for record in records:
        name = record['interface']
        qualified = 'ABI::Windows::UI::Xaml::' + ('Controls::' if name == 'ISwapChainPanel' else '') + name
        fields = record['iid'].split('-')
        tail = fields[3] + fields[4]
        values = [fields[0], fields[1], fields[2]] + [tail[i:i+2] for i in range(0,16,2)]
        parts.append('__CRT_UUID_DECL(' + qualified + ',' + ','.join('0x' + v for v in values) + ')\n')
    (output / 'windows.ui.xaml.h').write_text(''.join(parts))
    interop_text = dxinterop.read_text()
    interop = re.search(r'MIDL_INTERFACE\("([0-9A-Fa-f-]{36})"\)\s*ISwapChainPanelNative : public IUnknown\s*\{(.*?)\n\s*\};', interop_text, re.S)
    if not interop:
        raise ValueError('SDK SwapChainPanel native ABI missing')
    (output / 'windows.ui.xaml.media.dxinterop.h').write_text(
        '#pragma once\n#include <dxgi.h>\nMIDL_INTERFACE("' + interop[1]
        + '") ISwapChainPanelNative : public IUnknown {\n' + interop[2] + '\n};\n')
    fields = interop[1].split('-')
    tail = fields[3] + fields[4]
    values = [fields[0], fields[1], fields[2]] + [tail[i:i+2] for i in range(0,16,2)]
    with (output / 'windows.ui.xaml.media.dxinterop.h').open('a') as stream:
        stream.write('__CRT_UUID_DECL(ISwapChainPanelNative,' + ','.join('0x' + v for v in values) + ')\n')
    inputs = [projection / 'impl/Windows.UI.Xaml.0.h', projection / 'impl/Windows.UI.Xaml.Controls.0.h',
              sdk_xaml, sdk_controls, dxinterop]
    (output / 'xaml-abi-provenance.json').write_text(json.dumps({
        'inputHashes': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        'interfaces': records, 'nativeInteropIid': interop[1],
        'implementationsOrStubsGenerated': False,
        'allInterfaceMethodsRetained': True,
        'opaquePointerTypesUseCppWinrtWireAbi': True,
    }, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--projection', type=Path, required=True)
    parser.add_argument('--sdk-xaml', type=Path, required=True)
    parser.add_argument('--sdk-controls', type=Path, required=True)
    parser.add_argument('--dxinterop', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.projection, args.sdk_xaml, args.sdk_controls, args.dxinterop, args.output)
