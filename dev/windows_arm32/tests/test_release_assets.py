"""Lossless release transport tests; no SDK execution is established here."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_assets import prepare_assets


class ReleaseAssetsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=os.environ['TMPDIR'])
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_small_archive_keeps_original_asset(self):
        archive = self.root / 'sdk.zip'
        archive.write_bytes(b'unchanged')
        self.assertEqual(prepare_assets(archive, maximum=9), ([archive], ''))
        self.assertEqual(list(self.root.iterdir()), [archive])

    def test_oversize_archive_reconstructs_with_complete_checksums(self):
        archive = self.root / 'flutter-windows-arm32-windows-x64.zip'
        original = bytes(range(256)) * 5 + b'last partial volume'
        archive.write_bytes(original)
        assets, instructions = prepare_assets(archive, maximum=600, part_size=500)
        manifest = json.loads(assets[-1].read_text())
        restored = b''
        for part in manifest['parts']:
            body = (self.root / part['name']).read_bytes()
            self.assertEqual(len(body), part['bytes'])
            self.assertLessEqual(len(body), 500)
            self.assertEqual(hashlib.sha256(body).hexdigest(), part['sha256'])
            restored += body
        self.assertEqual(restored, original)
        self.assertEqual(hashlib.sha256(restored).hexdigest(), manifest['sha256'])
        self.assertEqual(archive.read_bytes(), original)
        self.assertIn('cmd /c copy /b ', instructions)
        self.assertIn('+'.join(part['name'] for part in manifest['parts']), instructions)

    def test_existing_parts_are_preserved(self):
        archive = self.root / 'sdk.tar.gz'
        archive.write_bytes(b'123456789')
        part = self.root / 'sdk.tar.gz.part001'
        part.write_bytes(b'keep this')
        with self.assertRaises(FileExistsError):
            prepare_assets(archive, maximum=5, part_size=4)
        self.assertEqual(part.read_bytes(), b'keep this')

    def test_unix_reconstruction_uses_explicit_order(self):
        archive = self.root / 'sdk.tar.gz'
        archive.write_bytes(b'123456789')
        _, instructions = prepare_assets(archive, maximum=5, part_size=4)
        self.assertIn('cat sdk.tar.gz.part001 sdk.tar.gz.part002 sdk.tar.gz.part003 > sdk.tar.gz', instructions)


if __name__ == '__main__':
    unittest.main()
