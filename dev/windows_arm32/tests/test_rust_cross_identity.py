import copy
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
import rust_cross_identity as identity
import prepare_cargokit_rustup
from sdk_hosts import select_host


class CrossIdentityTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.destination = root / 'macos/bin/rustc'
        self.reference = root / 'linux/bin/rustc'
        self.host = select_host('macos-x64')
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        elf[18:20] = b'\x3e\0'
        self.destination.parent.mkdir(parents=True)
        self.reference.parent.mkdir(parents=True)
        self.destination.write_bytes(bytes.fromhex('cffaedfe') + struct.pack('<I', 0x01000007))
        self.reference.write_bytes(elf)
        self.channel = '''manifest-version = "2"
[pkg.rustc]
version = "1.95.0 (59807616e 2026-04-14)"
[pkg.rustc.target.x86_64-apple-darwin]
available = true
xz_hash = "rustc-input"
[pkg.cargo.target.x86_64-apple-darwin]
available = true
xz_hash = "cargo-input"
[pkg.rust-std.target.x86_64-apple-darwin]
available = true
xz_hash = "std-input"
[pkg.rust-src.target."*"]
available = true
'''
        for compiler in (self.destination, self.reference):
            channel = compiler.parent.parent / identity.CHANNEL_PATH
            channel.parent.mkdir(parents=True)
            channel.write_text(self.channel)
        self.info = ('rustc 1.95.0 (59807616e 2026-04-14)\n'
                     'release: 1.95.0\nhost: x86_64-unknown-linux-gnu\n'
                     'commit-hash: 59807616e123456789012345678901234567890123\n'
                     'commit-date: 2026-04-14\n')
        self.target = {'arch': 'arm', 'binary-format': 'coff', 'env': 'msvc',
                       'llvm-target': 'thumbv7a-pc-windows-msvc', 'target-pointer-width': 32,
                       'features': '+v7,+thumb2,+neon', 'panic-strategy': 'unwind'}
        self.native = mock.patch.object(identity, 'current_host',
                                        return_value=select_host('linux-x64'))
        self.native.start()
        self.addCleanup(self.native.stop)

    def inspect(self, outputs=None):
        with mock.patch.object(identity.subprocess, 'check_output',
                               side_effect=outputs or [self.info, json.dumps(self.target)]) as execute:
            result = identity.inspect(self.destination, '1.95.0', self.host, self.reference)
        return result, execute.call_args_list

    def test_foreign_compiler_never_executes_and_capabilities_are_preserved(self):
        # TOML serialization order/spacing is not source identity.
        manifest = self.destination.parent.parent / identity.CHANNEL_PATH
        manifest.write_text(self.channel.replace('manifest-version = "2"', 'manifest-version="2"'))
        result, calls = self.inspect()
        self.assertTrue(all(call.args[0][0] == str(self.reference) for call in calls))
        self.assertEqual(len(calls), 2)
        self.assertEqual(result['upstreamTarget'], self.target)
        self.assertFalse(result['compilerExecutionVerified'])
        self.assertIsNone(result['compilerIdentity'])
        self.assertEqual(result['host'], 'x86_64-apple-darwin')
        self.assertEqual(result['referenceCompilerIdentity'], self.info)

    def test_different_channel_fails_before_execution(self):
        manifest = self.destination.parent.parent / identity.CHANNEL_PATH
        manifest.write_text(self.channel.replace('59807616e', 'aaaaaaaaa'))
        with mock.patch.object(identity.subprocess, 'check_output') as execute:
            with self.assertRaisesRegex(ValueError, 'channels differ'):
                identity.inspect(self.destination, '1.95.0', self.host, self.reference)
            execute.assert_not_called()

    def test_private_rustup_home_preserves_cross_identity_without_executing_rustc(self):
        compiler = self.destination
        compiler.with_name('rustdoc').write_bytes(compiler.read_bytes())
        record, _ = self.inspect()
        root = compiler.parent.parent
        record['files'] = {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in root.rglob('*') if path.is_file()}
        (root / 'rust-distribution-provenance.json').write_text(json.dumps(record))
        config = root.parent / 'cargo.json'
        config.write_text(json.dumps({'toolchains': {'stable': {'rustc': str(compiler)}}}))
        output = root.parent / 'private-rustup'
        with mock.patch.object(prepare_cargokit_rustup.subprocess, 'check_output',
                               side_effect=AssertionError('foreign execution')):
            prepare_cargokit_rustup.prepare(config, output, cross_prepare=True)
            proof = json.loads((output / 'sdk-provenance.json').read_text())
            self.assertFalse(proof['compilerExecutionVerified'])
            self.assertEqual(proof['toolchains'][0]['host'], 'x86_64-apple-darwin')
            linked = output / 'toolchains/stable-x86_64-apple-darwin/bin/rustc'
            self.assertEqual(linked.resolve(), compiler.resolve())
            compiler.write_bytes(compiler.read_bytes() + b'tampered')
            with self.assertRaisesRegex(ValueError, 'identity file changed'):
                prepare_cargokit_rustup.prepare(config, root.parent / 'invalid-rustup', cross_prepare=True)

    def test_reference_must_match_declared_release_commit_date_and_host(self):
        for field, value in (('1.95.0\nhost:', '1.93.1\nhost:'),
                             ('59807616e1234', 'aaaaaaaaa1234'),
                             ('commit-date: 2026-04-14', 'commit-date: 2026-04-15'),
                             ('host: x86_64-unknown-linux-gnu', 'host: x86_64-apple-darwin')):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'declared Rust source'):
                self.inspect([self.info.replace(field, value)])

    def test_wrong_target_does_not_become_an_arm32_configuration(self):
        target = copy.deepcopy(self.target)
        target['arch'] = 'aarch64'
        with self.assertRaisesRegex(ValueError, 'upstream ARM32 target'):
            self.inspect([self.info, json.dumps(target)])

    def test_changed_reference_is_not_certified(self):
        def execute(command, **kwargs):
            if command[-1] == '-vV':
                return self.info
            self.reference.write_bytes(self.reference.read_bytes() + b'changed')
            return json.dumps(self.target)
        with mock.patch.object(identity.subprocess, 'check_output', side_effect=execute):
            with self.assertRaisesRegex(ValueError, 'changed during inspection'):
                identity.inspect(self.destination, '1.95.0', self.host, self.reference)

    def test_same_compiler_and_wrong_binary_architecture_are_rejected(self):
        with mock.patch.object(identity.subprocess, 'check_output') as execute:
            with self.assertRaisesRegex(ValueError, 'distinct'):
                identity.inspect(self.destination, '1.95.0', self.host, self.destination)
            self.reference.write_bytes(self.destination.read_bytes())
            with self.assertRaisesRegex(ValueError, 'does not match SDK host'):
                identity.inspect(self.destination, '1.95.0', self.host, self.reference)
            execute.assert_not_called()

    def test_installed_evidence_does_not_survive_binary_change_or_fabricated_execution(self):
        record, _ = self.inspect()
        root = self.destination.parent.parent
        record['files'] = {name: identity.sha(root / name)
                           for name in ('bin/rustc', str(identity.CHANNEL_PATH))}
        provenance = root / 'rust-distribution-provenance.json'
        provenance.write_text(json.dumps(record))
        self.assertEqual(identity.load(self.destination)['upstreamTarget'], self.target)
        body = self.destination.read_bytes()
        self.destination.write_bytes(body + b'changed')
        with self.assertRaisesRegex(ValueError, 'identity file changed'):
            identity.load(self.destination)
        self.destination.write_bytes(body)
        record['compilerExecutionVerified'] = True
        provenance.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'explicit cross-preparation evidence'):
            identity.load(self.destination)


if __name__ == '__main__':
    unittest.main()
