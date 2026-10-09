import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / 'toolchain')]
import prepare_rust_distribution as distribution
import prepare_rust
import rust_cross_identity
import macho_native_dependencies
from sdk_hosts import select_host


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.info = ('rustc 1.95.0 (59807616e 2026-04-14)\n'
                     'release: 1.95.0\nhost: x86_64-unknown-linux-gnu\n'
                     'commit-hash: 59807616e00000000000000000000000000000000\n'
                     'commit-date: 2026-04-14\n')
        self.target = {'arch': 'arm', 'binary-format': 'coff', 'env': 'msvc',
                       'llvm-target': 'thumbv7a-pc-windows-msvc', 'target-pointer-width': 32,
                       'panic-strategy': 'unwind'}

    def installation(self, name, host):
        root = self.root / name
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        elf[18:20] = b'\x3e\0'
        header = bytes(elf) if host == 'linux-x64' else bytes.fromhex('cffaedfe') + struct.pack('<I', 0x01000007)
        triple = select_host(host)['rustHost']
        payload = {'bin/rustc': header, 'bin/rustdoc': header, 'bin/cargo': header,
                   'share/doc/rust/COPYRIGHT': b'component notice preserved',
                   'lib/rustlib/' + triple + '/lib/libstd.rlib': b'!<arch>\n',
                   'lib/rustlib/src/rust/library/Cargo.toml': b'[workspace]\n'}
        for name, body in payload.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
        for component, entries in (
                ('rustc-' + triple, ['file:bin/rustc', 'file:bin/rustdoc', 'dir:share/doc/rust']),
                ('cargo-' + triple, ['file:bin/cargo']),
                ('rust-std-' + triple, ['dir:lib/rustlib/' + triple]),
                ('rust-src', ['dir:lib/rustlib/src'])):
            (root / 'lib/rustlib' / ('manifest-' + component)).write_text('\n'.join(entries) + '\n')
        channel = 'manifest-version="2"\n[pkg.rustc]\nversion="1.95.0 (59807616e 2026-04-14)"\n'
        for component in ('rustc', 'cargo', 'rust-std'):
            channel += ('[pkg.' + component + '.target.x86_64-apple-darwin]\n'
                        'available=true\nxz_hash="' + 'a' * 64 + '"\n')
        channel += '[pkg.rust-src.target."*"]\navailable=true\n'
        (root / rust_cross_identity.CHANNEL_PATH).write_text(channel)
        return root / 'bin/rustc'

    def test_native_default_still_executes_original_and_copied_compilers(self):
        rustc = self.installation('native', 'linux-x64')
        out = self.root / 'native-output'
        with mock.patch.object(distribution.subprocess, 'check_output', return_value=self.info) as execute, \
                mock.patch.object(distribution, 'audit_native', return_value=[]):
            distribution.prepare(rustc, out, '1.95.0', host='linux-x64')
        self.assertEqual([call.args[0] for call in execute.call_args_list],
                         [[str(rustc), '-vV'], [str(out / 'bin/rustc'), '-vV']])
        record = json.loads((out / 'rust-distribution-provenance.json').read_text())
        self.assertTrue(record['compilerExecutionVerified'])
        self.assertEqual(record['compilerIdentity'], self.info)
        self.assertNotIn(str(rust_cross_identity.CHANNEL_PATH), record['files'])
        self.assertEqual((out / 'share/doc/rust/COPYRIGHT').read_bytes(), b'component notice preserved')

    def test_foreign_copy_and_relocation_keep_all_component_files_without_destination_execution(self):
        rustc = self.installation('macos', 'macos-x64')
        reference = self.installation('reference', 'linux-x64')
        original = {str(p.relative_to(rustc.parent.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in rustc.parent.parent.rglob('*') if p.is_file()}
        out = self.root / 'foreign-output'
        options = dict(host='macos-x64', reference_rustc=reference,
                       llvm_objdump=Path('/usr/bin/llvm-objdump'))
        with mock.patch.object(distribution.subprocess, 'check_output',
                               side_effect=[self.info, json.dumps(self.target)] * 2) as execute, \
                mock.patch.object(macho_native_dependencies, 'audit', return_value=[]), \
                mock.patch.object(rust_cross_identity, 'current_host', return_value=select_host('linux-x64')):
            distribution.prepare(rustc, out, '1.95.0', **options)
            moved = self.root / 'relocated Rust 日本語'
            out.rename(moved)
            distribution.prepare(rustc, moved, '1.95.0', verify_existing=True, **options)
        self.assertTrue(all(call.args[0][0] == str(reference) for call in execute.call_args_list))
        record = rust_cross_identity.load(moved / 'bin/rustc')
        self.assertFalse(record['compilerExecutionVerified'])
        self.assertIn('share/doc/rust/COPYRIGHT', record['files'])
        self.assertIn(str(rust_cross_identity.CHANNEL_PATH), record['files'])
        self.assertEqual(record['upstreamTarget'], self.target)
        self.assertEqual(original, {str(p.relative_to(rustc.parent.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in rustc.parent.parent.rglob('*') if p.is_file()})

    def test_cross_mode_does_not_accept_a_destination_runner_or_missing_reader(self):
        rustc = self.installation('macos', 'macos-x64')
        reference = self.installation('reference', 'linux-x64')
        out = self.root / 'must-not-exist'
        with mock.patch.object(distribution.subprocess, 'check_output') as execute:
            for options in ({'runner': ['wine'], 'llvm_objdump': Path('reader')}, {}):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    distribution.prepare(rustc, out, '1.95.0', host='macos-x64',
                                         reference_rustc=reference, **options)
            execute.assert_not_called()
        self.assertFalse(out.exists())

    def test_cross_target_specs_equal_native_specs_and_only_reference_parses_them(self):
        rustc = self.installation('macos', 'macos-x64')
        reference = self.installation('reference', 'linux-x64')
        out = self.root / 'prepared'
        with mock.patch.object(distribution.subprocess, 'check_output',
                               side_effect=[self.info, json.dumps(self.target)]), \
                mock.patch.object(macho_native_dependencies, 'audit', return_value=[]), \
                mock.patch.object(rust_cross_identity, 'current_host', return_value=select_host('linux-x64')):
            distribution.prepare(rustc, out, '1.95.0', host='macos-x64', reference_rustc=reference,
                                 llvm_objdump=Path('/usr/bin/llvm-objdump'))
        values = {}
        for key in ('compiler', 'llvmBin', 'sysroot', 'resourceDir'):
            path = self.root / key
            path.mkdir()
            values[key] = str(path)
        configuration = self.root / 'native-toolchain.json'
        configuration.write_text(json.dumps(values))
        cross_targets, native_targets = self.root / 'cross-targets', self.root / 'native-targets'
        with mock.patch.object(prepare_rust.subprocess, 'check_output',
                               side_effect=[self.info, json.dumps(self.target)]) as execute, \
                mock.patch.object(prepare_rust.subprocess, 'run') as validate, \
                mock.patch.object(rust_cross_identity, 'current_host', return_value=select_host('linux-x64')):
            prepare_rust.prepare(cross_targets, out / 'bin/rustc', configuration,
                                 reference_rustc=reference)
        self.assertTrue(all(call.args[0][0] == str(reference) for call in execute.call_args_list))
        self.assertEqual(len(validate.call_args_list), 2)
        self.assertTrue(all(call.args[0][0] == str(reference) for call in validate.call_args_list))
        with mock.patch.object(prepare_rust.subprocess, 'check_output',
                               side_effect=[self.info, json.dumps(self.target)]), \
                mock.patch.object(prepare_rust.subprocess, 'run'):
            prepare_rust.prepare(native_targets, reference, configuration)
        for family in ('rt', 'phone'):
            name = 'thumbv7a-' + family + '-windows-msvc.json'
            self.assertEqual((cross_targets / name).read_bytes(), (native_targets / name).read_bytes())
        evidence = json.loads((cross_targets / 'rust-target-provenance.json').read_text())
        self.assertFalse(evidence['destinationCompilerExecutionVerified'])
        self.assertIsNone(evidence['rustCompilerVersion'])


if __name__ == '__main__':
    unittest.main()
