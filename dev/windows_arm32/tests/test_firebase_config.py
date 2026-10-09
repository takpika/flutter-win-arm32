#!/usr/bin/env python3
"""Public Firebase resource extraction; source applications remain unchanged."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('firebase_configuration', Path(__file__).resolve().parents[1] / 'prepare_firebase_config.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def services(package='org.example.synthetic', cert=True):
    client = {'client_info': {'android_client_info': {'package_name': package},
                              'mobilesdk_app_id': '1:123:android:synthetic'},
              'api_key': [{'current_key': 'synthetic-public-client-key'}]}
    if cert:
        client['oauth_client'] = [{'client_type': 1, 'android_info': {
            'package_name': package, 'certificate_hash': 'ab' * 20}}]
    return {'project_info': {'project_id': 'synthetic-project', 'project_number': '123',
                             'storage_bucket': 'synthetic-bucket'}, 'client': [client]}


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        temporary = Path(os.environ.get('TMPDIR', Path(__file__).resolve().parents[3] / 'build/tmp'))
        temporary.mkdir(parents=True, exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(prefix='firebase-config-', dir=temporary)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.google = self.root / 'android/app/google-services.json'
        self.google.parent.mkdir(parents=True)

    def write(self, value):
        self.google.write_text(json.dumps(value), encoding='utf-8')

    def external(self, entry):
        value = {'appName': '[DEFAULT]', 'googleServicesFile': 'android/app/google-services.json', **entry}
        file = self.root / 'firebase-input.json'
        file.write_text(json.dumps({'version': 1, 'apps': [value]}), encoding='utf-8')
        return file

    def test_automatic_original_resources_no_mutation(self):
        self.write(services())
        before = hashlib.sha256(self.google.read_bytes()).digest()
        result = MODULE.prepare(self.root, messaging=True, display_name='日本語アプリ')
        app = result['apps'][0]
        self.assertEqual(app['messaging']['certificateSha1'], 'ab' * 20)
        self.assertEqual(app['displayName'], '日本語アプリ')
        self.assertEqual(app['coreOptions']['storageBucket'], 'synthetic-bucket')
        self.assertEqual(app['messaging']['senderId'], '123')
        self.assertNotIn(str(self.root), json.dumps(result))
        self.assertEqual(before, hashlib.sha256(self.google.read_bytes()).digest())

    def test_core_resources_do_not_require_android_signing(self):
        self.write(services(cert=False))
        result = MODULE.prepare(self.root)
        self.assertNotIn('messaging', result['apps'][0])
        with self.assertRaisesRegex(ValueError, 'certificate SHA-1'):
            MODULE.prepare(self.root, messaging=True)

    def test_missing_resources_truthful(self):
        self.assertIsNone(MODULE.prepare(self.root))
        with self.assertRaisesRegex(ValueError, 'google-services'):
            MODULE.prepare(self.root, messaging=True)

    def test_multiple_packages_and_certificates_need_explicit_selection(self):
        original = services()
        original['client'] += services('org.example.second')['client']
        self.write(original)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            MODULE.prepare(self.root, messaging=True)
        external = self.external({'androidPackage': 'org.example.second', 'certificateSha1': 'CD:' * 19 + 'CD'})
        result = MODULE.prepare(self.root, external, messaging=True)
        self.assertEqual(result['apps'][0]['messaging']['androidPackage'], 'org.example.second')
        self.assertEqual(result['apps'][0]['messaging']['certificateSha1'], 'cd' * 20)

    def test_external_certificate_and_windows_core_options(self):
        self.write(services(cert=False))
        (self.root / 'certificate.txt').write_text('ef' * 20 + '\n')
        external = self.external({'certificateSha1File': 'certificate.txt', 'coreOptions': {
            'apiKey': 'synthetic-windows-key', 'appId': '1:123:web:windows',
            'projectId': 'synthetic-project', 'messagingSenderId': '123', 'iosClientId': None}})
        app = MODULE.prepare(self.root, external, messaging=True)['apps'][0]
        self.assertEqual(app['coreOptions']['appId'], '1:123:web:windows')
        self.assertEqual(app['messaging']['appId'], '1:123:android:synthetic')
        self.assertNotIn('iosClientId', app['coreOptions'])
        self.assertEqual(app['messaging']['certificateSha1'], 'ef' * 20)

    def test_secret_input_and_cross_project_resources_rejected(self):
        self.write({'type': 'service_account', 'private_key': 'synthetic-forbidden-key'})
        with self.assertRaisesRegex(ValueError, 'never a service account'):
            MODULE.prepare(self.root, messaging=True)
        self.write(services())
        external = self.external({'coreOptions': {'apiKey': 'public', 'appId': 'other-app',
            'projectId': 'different-project', 'messagingSenderId': '123'}})
        with self.assertRaisesRegex(ValueError, 'same Firebase project'):
            MODULE.prepare(self.root, external, messaging=True)
        external = self.external({'refreshToken': 'synthetic-forbidden-token'})
        with self.assertRaisesRegex(ValueError, 'public client'):
            MODULE.prepare(self.root, external, messaging=True)

    def test_original_numeric_sender_and_invalid_certificate(self):
        value = services(); value['project_info']['project_number'] = 123
        self.write(value)
        self.assertEqual(MODULE.prepare(self.root, messaging=True)['apps'][0]['messaging']['senderId'], '123')
        external = self.external({'certificateSha1': 'wrong'})
        with self.assertRaisesRegex(ValueError, '40 hexadecimal'):
            MODULE.prepare(self.root, external, messaging=True)


if __name__ == '__main__':
    unittest.main()
