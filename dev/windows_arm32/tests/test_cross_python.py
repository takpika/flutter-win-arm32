import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / 'toolchain')]
import assemble_sdk
import prepare_phone
from prepare_python import inputs
from sdk_hosts import select_host


class CrossPythonTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.host = select_host('macos-arm64')
        self.python = self.root / 'bin/python3.14'
        self.python.parent.mkdir()
        self.python.write_bytes(bytes.fromhex('cffaedfe') + struct.pack('<I', 0x0100000c))
        self.metadata = self.root / 'lib/python3.14/site-packages/pillow-12.3.0.dist-info/METADATA'
        self.metadata.parent.mkdir(parents=True)
        self.metadata.write_text('Name: pillow\nVersion: 12.3.0\n')
        self.foreign_module = self.metadata.parent.parent / 'PIL/__init__.py'
        self.foreign_module.parent.mkdir()
        self.foreign_module.write_text('raise RuntimeError("must not import foreign extensions")\n')
        self.record = {'host': self.host['id'], **{key: inputs(self.host)[key]
                       for key in ('pythonVersion', 'archiveSha256')}}
        self.record['pillowWheelSha256'] = inputs(self.host)['wheelSha256']
        self.write_record()

    def write_record(self):
        self.record['files'] = {str(p.relative_to(self.root)): assemble_sdk.digest(p)
                                for p in (self.metadata, self.foreign_module, self.python)}
        (self.root / 'python-provenance.json').write_text(json.dumps(self.record))

    def test_pinned_files_validate_without_running_destination_python(self):
        with mock.patch.object(assemble_sdk.subprocess, 'run', side_effect=AssertionError('foreign execution')):
            assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')

    def test_phone_configuration_uses_the_same_pinned_foreign_python_audit(self):
        self.python.chmod(0o755)
        runtime = self.root / 'runtime'
        runtime.mkdir()
        for name in prepare_phone.RUNTIME_FILES:
            (runtime / name).write_bytes(b'fixture-runtime')
        packager = self.root / 'makemsix'
        packager.write_bytes(b'fixture-packager'); packager.chmod(0o755)
        config = self.root / 'native-toolchain.json'
        config.write_text(json.dumps({key: str(self.root) for key in
                                     ('compiler', 'llvmBin', 'sysroot', 'resourceDir')}))
        with mock.patch.object(prepare_phone.subprocess, 'run', side_effect=AssertionError('foreign execution')), \
                mock.patch.dict(sys.modules, {'assemble_sdk': None}):
            result = prepare_phone.prepare(self.root / 'phone', config, runtime, self.python,
                                           packager, 'makemsix', {}, cross_prepare_host=self.host['id'])
            self.assertTrue(result.is_file())
            self.foreign_module.write_text('changed after distribution audit')
            with self.assertRaisesRegex(ValueError, 'Python file changed'):
                prepare_phone.prepare(self.root / 'invalid-phone', config, runtime, self.python,
                                      packager, 'makemsix', {}, cross_prepare_host=self.host['id'])
        self.assertFalse((self.root / 'invalid-phone').exists())

    def test_host_and_wheel_pin_mismatch_are_rejected(self):
        for field, value in (('host', 'linux-x64'), ('pillowWheelSha256', '0' * 64)):
            with self.subTest(field=field):
                original = self.record[field]
                self.record[field] = value
                self.write_record()
                with self.assertRaisesRegex(ValueError, 'pinned host inputs'):
                    assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')
                self.record[field] = original

    def test_modified_runtime_file_is_rejected(self):
        self.foreign_module.write_text('changed\n')
        with self.assertRaisesRegex(ValueError, 'file changed'):
            assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')

    def test_audited_but_wrong_or_ambiguous_package_version_is_rejected(self):
        for text in ('Name: pillow\nVersion: 12.2.0\n',
                     'Name: pillow\nVersion: 12.3.0\nVersion: 12.2.0\n',
                     'Name: another-package\nVersion: 12.3.0\n'):
            with self.subTest(text=text):
                self.metadata.write_text(text)
                self.write_record()
                with self.assertRaisesRegex(ValueError, 'Phone requirements'):
                    assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')

    def test_untracked_package_metadata_is_not_trusted(self):
        del self.record['files'][str(self.metadata.relative_to(self.root))]
        (self.root / 'python-provenance.json').write_text(json.dumps(self.record))
        with self.assertRaisesRegex(ValueError, 'audited Pillow'):
            assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')

    def test_legacy_record_requires_pinned_inputs_and_matching_actual_binary(self):
        del self.record['host']
        self.write_record()
        assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')
        self.python.write_bytes(bytes.fromhex('cffaedfe') + struct.pack('<I', 0x01000007))
        self.write_record()
        with self.assertRaisesRegex(ValueError, 'does not match SDK host'):
            assemble_sdk.verify_cross_python(self.root, self.host, '12.3.0')


if __name__ == '__main__':
    unittest.main()
