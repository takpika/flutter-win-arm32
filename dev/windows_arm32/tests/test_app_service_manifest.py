"""Verify in-process App Service declarations without changing ordinary apps."""
import os
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_uwp import create_assets_and_manifest, FOUNDATION, UAP
from firebase_consumer import load as load_consumer, dependency as consumer_dependency


class AppServiceManifestTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            dir=os.environ.get('TMPDIR', str(Path(__file__).parent)))
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.icon = self.root / 'icon.png'
        Image.new('RGBA', (64, 64), 'blue').save(self.icon)
        self.metadata = {'identityName': 'Synthetic.Flutter.Manifest',
                         'version': '1.0.0.1', 'publisher': 'CN=Synthetic',
                         'publisherDisplayName': 'Synthetic', 'displayName': 'Synthetic'}

    def create(self, services=(), dependencies=()):
        bundle = self.root / 'bundle'
        bundle.mkdir(exist_ok=True)
        create_assets_and_manifest(bundle, 'synthetic', self.metadata, self.icon,
                                   app_services=services, package_dependencies=dependencies)
        return ET.parse(bundle / 'AppxManifest.xml')

    def test_ordinary_app_retains_minimum_and_has_no_extensions(self):
        document = self.create()
        self.assertIsNone(document.find('.//{' + FOUNDATION + '}Extensions'))
        self.assertIsNone(document.find('.//{' + FOUNDATION + '}PackageDependency'))
        self.assertEqual(document.find('.//{' + FOUNDATION + '}TargetDeviceFamily')
                         .get('MinVersion'), '10.0.10240.0')

    def test_service_uses_in_process_activation_on_supported_os(self):
        document = self.create(('wpnotify.deliver',))
        extension = document.find('.//{' + UAP + '}Extension')
        self.assertEqual(extension.get('Category'), 'windows.appService')
        self.assertNotIn('EntryPoint', extension.attrib)
        self.assertEqual(extension.find('{' + UAP + '}AppService').get('Name'),
                         'wpnotify.deliver')
        self.assertEqual(document.find('.//{' + FOUNDATION + '}TargetDeviceFamily')
                         .get('MinVersion'), '10.0.14393.0')

    def test_newer_application_minimum_preserved(self):
        self.metadata['minVersion'] = '10.0.15254.0'
        self.assertEqual(self.create(('wpnotify.deliver',)).find(
            './/{' + FOUNDATION + '}TargetDeviceFamily').get('MinVersion'),
            '10.0.15254.0')

    def test_invalid_or_duplicate_services_rejected(self):
        for services in [('wpnotify.deliver', 'wpnotify.deliver'), ('invalid name',)]:
            with self.subTest(services=services), self.assertRaises(ValueError):
                self.create(services)

    def test_framework_dependency_uses_exact_distribution_identity(self):
        dependency = {'Name': 'Synthetic.Firebase.Framework',
                      'Publisher': 'CN=Synthetic & Testing', 'MinVersion': '1.2.3.4',
                      'MaxMajorVersionTested': '1'}
        document = self.create(dependencies=(dependency,))
        node = document.find('.//{' + FOUNDATION + '}PackageDependency')
        self.assertEqual(node.attrib, dependency)
        self.assertEqual(document.find('.//{' + FOUNDATION + '}TargetDeviceFamily')
                         .get('MinVersion'), '10.0.10240.0')
        self.assertIsNone(document.find('.//{' + FOUNDATION + '}Extensions'))

    def test_shared_sdk_dependency_matches_verified_distribution(self):
        sdk = Path(__file__).resolve().parents[3]
        directory, contract = load_consumer(sdk)
        expected = ET.parse(directory / 'PackageDependency.xml').getroot().attrib
        node = self.create(dependencies=(consumer_dependency(contract),)).find(
            './/{' + FOUNDATION + '}PackageDependency')
        self.assertEqual(node.attrib, expected)
        self.assertFalse(contract['embedDll'])
        self.assertFalse(any(p.suffix == '.dll' for p in directory.rglob('*')))

    def test_changed_consumer_import_library_rejected_before_use(self):
        sdk = Path(__file__).resolve().parents[3]
        directory, contract = load_consumer(sdk)
        copied_sdk = self.root / 'sdk'
        copied = copied_sdk / directory.relative_to(sdk)
        shutil.copytree(directory, copied)
        library = copied / contract['importLibrary']
        data = library.read_bytes()
        library.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        with self.assertRaisesRegex(ValueError, 'Changed Firebase consumer input'):
            load_consumer(copied_sdk)

    def test_invalid_or_duplicate_framework_dependencies_rejected(self):
        dependency = {'Name': 'Synthetic.Firebase.Framework',
                      'Publisher': 'CN=Synthetic', 'MinVersion': '1.2.3.4'}
        invalid = [
            {'Name': 'bad name'}, {'Publisher': ''}, {'MinVersion': '1.2.3'},
            {'MinVersion': '1.2.3.65536'}, {'MinVersion': '1.2.3.-1'},
            {'MinVersion': 1}, {'MaxMajorVersionTested': '65536'},
            {'Optional': 'true'},
        ]
        for change in invalid:
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.create(dependencies=({**dependency, **change},))
        for duplicate in (dependency, {**dependency, 'Name': dependency['Name'].upper()}):
            with self.assertRaises(ValueError):
                self.create(dependencies=(dependency, duplicate))

    def test_framework_and_in_process_service_coexist(self):
        dependency = {'Name': 'Synthetic.Firebase.Framework',
                      'Publisher': 'CN=Synthetic', 'MinVersion': '1.2.3.4'}
        document = self.create(('wpnotify.deliver',), (dependency,))
        self.assertEqual(document.find('.//{' + FOUNDATION + '}PackageDependency').attrib,
                         dependency)
        self.assertEqual(document.find('.//{' + UAP + '}AppService').get('Name'),
                         'wpnotify.deliver')
        self.assertEqual(document.find('.//{' + FOUNDATION + '}TargetDeviceFamily')
                         .get('MinVersion'), '10.0.14393.0')


if __name__ == '__main__':
    unittest.main()
