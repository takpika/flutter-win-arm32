#!/usr/bin/env python3
"""Windows ARM SDK snapshot backend: generate A32, then verified fixed-slot Thumb-2."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from elf_code_ranges import code_ranges


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def text_section(image):
    section_offset = struct.unpack_from('<I', image, 32)[0]
    size, count, names_index = struct.unpack_from('<HHH', image, 46)
    sections = [struct.unpack_from('<10I', image, section_offset + i * size) for i in range(count)]
    names = sections[names_index]
    strings = image[names[4]:names[4] + names[5]]
    for section in sections:
        if strings[section[0]:].split(b'\0', 1)[0] == b'.text':
            return section[3], section[4], section[5]
    raise ValueError('Missing ELF text section')


def main():
    directory = Path(__file__).resolve().parent
    config = json.loads((directory / 'gen_snapshot_config.json').read_text())
    # Local verification may restrict outputs to a workspace. A distributed SDK
    # must also work for application repositories outside its install directory.
    root = Path(config['workspaceRoot']).resolve() if config.get('workspaceRoot') else None
    raw_generator = directory / config.get('rawGenerator', 'gen_snapshot_raw')
    if digest(raw_generator) != config['rawGeneratorSha256']:
        raise ValueError('SDK snapshot generator differs from its recorded source build')
    arguments = sys.argv[1:]
    outputs = [argument[6:] for argument in arguments if argument.startswith('--elf=')]
    if not outputs:
        return subprocess.call([str(raw_generator), *arguments])
    if len(outputs) != 1 or '--snapshot_kind=app-aot-elf' not in arguments:
        raise ValueError('Windows ARM requires one app-aot-elf output')
    output = Path(outputs[0]).resolve()
    if root is not None and not output.is_relative_to(root):
        raise ValueError('SDK snapshot outputs must remain in the SSD workspace')
    output.parent.mkdir(parents=True, exist_ok=True)
    stripping = '--strip' in arguments
    llvm = Path(config['llvmBin'])
    if not llvm.is_absolute():
        llvm = (directory / llvm).resolve()
    environment = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='windows-arm-aot-', dir=output.parent) as scratch:
        work = Path(scratch)
        environment.update(TMPDIR=str(work), TMP=str(work), TEMP=str(work))
        unstripped = work / 'unstripped.so'
        obj = work / 'thumb.obj'
        generated = work / 'generated.so'
        patched = work / 'thumb.so'
        def generator_arguments(destination, strip):
            return ['--elf=' + str(destination) if arg.startswith('--elf=') else arg
                    for arg in arguments if strip or arg != '--strip']
        with (work / 'conversion.log').open('w') as log:
            def run(command):
                try:
                    subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
                except subprocess.CalledProcessError:
                    log.flush()
                    output.with_suffix(output.suffix + '.conversion.log').write_bytes((work / 'conversion.log').read_bytes())
                    raise
            run([str(raw_generator), *generator_arguments(unstripped, False)])
            ranges = code_ranges(unstripped.read_bytes())
            if stripping:
                # Let Dart itself implement --strip. Its instruction bytes must
                # match the unstripped generation used to locate function ranges.
                run([str(raw_generator), *generator_arguments(generated, True)])
                a, offset_a, size_a = text_section(unstripped.read_bytes())
                b, offset_b, size_b = text_section(generated.read_bytes())
                if (a, size_a) != (b, size_b) or unstripped.read_bytes()[offset_a:offset_a+size_a] != generated.read_bytes()[offset_b:offset_b+size_b]:
                    raise ValueError('Stripping changed generated instruction bytes or addresses')
            else:
                generated.write_bytes(unstripped.read_bytes())
            range_arguments = [item for start, stop in ranges for item in ('--range', f'{start:#x}:{stop:#x}')]
            before = digest(generated)
            run([sys.executable, str(directory / 'lower_a32_code_ranges_to_fixed_thumb2.py'),
                 str(unstripped), '--out-asm', str(work / 'thumb.s'), '--out-obj', str(obj),
                 '--assembly-stdin',
                 '--objdump', str(llvm / 'llvm-objdump'), '--llvm-mc', str(llvm / 'llvm-mc'),
                 '--objcopy', str(llvm / 'llvm-objcopy'), *range_arguments])
            # Diagnostic file-mode conversion may still leave its assembly.
            (work / 'thumb.s').unlink(missing_ok=True)
            object_digest = digest(obj)
            run([sys.executable, str(directory / 'patch_app_so_with_fixed_thumb2_text.py'),
                 str(generated), str(obj), str(patched), '--objdump', str(llvm / 'llvm-objdump'),
                 '--objcopy', str(llvm / 'llvm-objcopy'), *range_arguments])
            run([sys.executable, str(directory / 'check_fixed_thumb2_aot.py'), str(generated), str(obj),
                 str(patched), '--readelf', str(llvm / 'llvm-readelf'),
                 '--objcopy', str(llvm / 'llvm-objcopy'), *range_arguments])
            original, actual = generated.read_bytes(), patched.read_bytes()
            address, offset, _ = text_section(original)
            if len(original) != len(actual):
                raise ValueError('Thumb conversion changed ELF length')
            cursor = 0
            for start, stop in ranges:
                first, last = offset + start - address, offset + stop - address
                if original[cursor:first] != actual[cursor:first]:
                    raise ValueError('Thumb conversion changed bytes outside instruction ranges')
                cursor = last
            if original[cursor:] != actual[cursor:] or digest(generated) != before or digest(obj) != object_digest:
                raise ValueError('Snapshot integrity check failed')
            report = {'rawGeneratorSha256': config['rawGeneratorSha256'], 'strippingRequested': stripping,
                      'strippingImplementedByDartGenerator': stripping,
                      'originalAotSha256': before, 'thumbObjectSha256': object_digest,
                      'thumbAotSha256': digest(patched), 'codeRanges': ranges,
                      'allBytesOutsideConvertedRangesUnchanged': True,
                      'originalAotAndThumbObjectHashesPreservedDuringPatchAndVerification': True,
                      'runtimeVerified': False}
            # Publish only after all conversion and integrity checks succeed.
            os.replace(patched, output)
            output.with_suffix(output.suffix + '.thumb-integrity.json').write_text(json.dumps(report, indent=2) + '\n')
        output.with_suffix(output.suffix + '.conversion.log').write_bytes((work / 'conversion.log').read_bytes())
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
