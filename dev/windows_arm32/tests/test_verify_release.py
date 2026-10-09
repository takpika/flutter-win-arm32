"""Release record requirements only; synthetic PE bytes prove no device behavior."""
import hashlib
import copy
import io
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_release import verify

OLD_PHONE = {'startup', 'touch_alignment', 'folder_picker', 'url_launcher', 'send', 'receive'}
ADDED_PHONE = {
    'safe_area', 'system_bars', 'theme', 'accent_color', 'continuum_capture',
    'continuum_hover', 'continuum_scroll', 'continuum_secondary_button',
    'continuum_resize', 'suspend_resume', 'fcm_foreground', 'fcm_background',
    'fcm_cold_start', 'fcm_initial_message', 'fcm_notification_tap',
    'fcm_token_lifecycle', 'fcm_topics', 'fcm_notification_settings', 'fcm_migration',
}


class ReleaseRequirementsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR', str(Path(__file__).parent)))
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.archive = self.root / 'synthetic.tar.gz'
        self.record = self.root / 'physical.json'
        self.proof = {'schemaVersion': 1, 'buildRunId': '1', 'sourceRevision': 'a' * 40, 'devices': {}}
        prefix = 'flutter/bin/cache/artifacts/engine/windows-arm-release/'
        members = {}
        for family, names in {
            'rt': ('flutter_windows.dll', 'libEGL.dll', 'libGLESv2.dll'),
            'phone': ('flutter_engine.dll', 'libEGL.dll', 'libGLESv2.dll', 'flutter_winrt_compat.dll'),
        }.items():
            device = {'hardwareAccelerated': True, 'softwareAdapter': False,
                      'adapterName': 'Synthetic record fixture', 'runtimeSha256': {},
                      'verifiedChecks': sorted({'startup', 'send', 'receive'} if family == 'rt' else OLD_PHONE | ADDED_PHONE)}
            self.proof['devices'][family] = device
            for name in names:
                content = b'MZ' + bytes(58) + (64).to_bytes(4, 'little') + b'PE\0\0\xc4\x01' + name.encode()
                device['runtimeSha256'][name] = hashlib.sha256(content).hexdigest()
                members[prefix + ('phone/' if family == 'phone' else '') + name] = content
        members[prefix + 'sdk-assembly.json'] = json.dumps({key: True for key in (
            'bothRuntimeFamiliesInstalled', 'sdkDependencyPathsPreflightPassed',
            'applicationBuildVerified', 'relocatedSdkBuildVerified')}).encode()
        self.members = members
        self.write_archive()

    def write_archive(self):
        with tarfile.open(self.archive, 'w:gz') as archive:
            for name, content in self.members.items():
                item = tarfile.TarInfo(name); item.size = len(content)
                archive.addfile(item, io.BytesIO(content))
        self.proof['sdkArchiveSha256'] = hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def check(self):
        self.record.write_text(json.dumps(self.proof))
        return verify(self.archive, self.record, '1', 'a' * 40)

    def test_complete_record_accepts_new_requirements(self):
        self.assertTrue(self.check()['releaseReady'])

    def test_previous_startup_and_transfer_record_is_incomplete(self):
        self.proof['devices']['phone']['verifiedChecks'] = sorted(OLD_PHONE)
        with self.assertRaisesRegex(ValueError, 'Incomplete physical application checks: phone'):
            self.check()

    def test_each_newly_requested_feature_is_required(self):
        for missing in sorted(ADDED_PHONE):
            with self.subTest(missing=missing):
                self.proof['devices']['phone']['verifiedChecks'] = sorted((OLD_PHONE | ADDED_PHONE) - {missing})
                with self.assertRaisesRegex(ValueError, 'Incomplete physical application checks: phone'):
                    self.check()

    def macos_record(self):
        prefix = 'flutter/bin/cache/'
        path = prefix + 'artifacts/engine/windows-arm-release/sdk-assembly.json'
        metadata = json.loads(self.members[path])
        metadata.update(applicationBuildVerified=False, relocatedSdkBuildVerified=False)
        self.members[path] = json.dumps(metadata).encode()
        self.members[prefix+'windows-arm32-host-bootstrap.json'] = json.dumps({
            'sdkHost':'macos-arm64', 'sourceRevision':'a'*40}).encode()
        self.members[prefix+'windows-arm32-cross-preparation.json'] = json.dumps({
            'host':'macos-arm64', 'nativeDestinationExecutionVerified':False,
            'relocatedApplicationBuildVerified':False}).encode()
        self.write_archive()
        native = {'schemaVersion':1, 'sdkHost':'macos-arm64', 'sourceRevision':'a'*40,
            'sdkArchiveSha256':self.proof['sdkArchiveSha256'], 'executionSystem':'Darwin',
            'executionArchitecture':'arm64',
            'applicationCommit':'5ccc6dea192d1c697c2602bf456b2eb2ad8e9674',
            'applicationSourcesUnmodified':True, 'sdkSourcesUnmodified':True,
            'nativeDependencyPathsPreflightPassed':True,
            'originalSdkPathUnavailableAfterRelocation':True, 'builds':[]}
        for phase in ('initial','relocated'):
            for family in ('rt','phone'):
                command = ['flutter','build','windows','--release','--target-platform','windows-arm']
                if family == 'phone':command += ['--windows-family','phone']
                native['builds'].append({'phase':phase,'family':family,'command':command,
                                        'exitCode':0,'logSha256':'b'*64})
        self.proof['nativeSdkValidation'] = native
        return native

    def test_cross_preparation_alone_never_proves_native_builds(self):
        self.macos_record()
        del self.proof['nativeSdkValidation']
        with self.assertRaisesRegex(ValueError,'Missing complete SDK'):
            self.check()

    def test_exact_archive_with_four_later_native_builds_is_accepted(self):
        self.macos_record()
        self.assertTrue(self.check()['nativeMacosSdkValidationAccepted'])

    def test_native_record_identity_and_execution_requirements(self):
        original = self.macos_record()
        changes = {'sdkArchiveSha256':'c'*64,'sourceRevision':'d'*40,
            'sdkHost':'macos-x64','executionArchitecture':'x86_64',
            'executionSystem':'Linux','applicationCommit':'e'*40,
            'applicationSourcesUnmodified':False,'sdkSourcesUnmodified':False,
            'nativeDependencyPathsPreflightPassed':False,
            'originalSdkPathUnavailableAfterRelocation':False}
        for field,value in changes.items():
            with self.subTest(field=field):
                self.proof['nativeSdkValidation'] = copy.deepcopy(original)
                self.proof['nativeSdkValidation'][field] = value
                with self.assertRaisesRegex(ValueError,'Missing complete SDK'):
                    self.check()

    def test_each_native_build_must_succeed_and_be_distinct(self):
        original = self.macos_record()
        for number in range(4):
            with self.subTest(build=number):
                self.proof['nativeSdkValidation'] = copy.deepcopy(original)
                self.proof['nativeSdkValidation']['builds'][number]['exitCode'] = 1
                with self.assertRaisesRegex(ValueError,'Missing complete SDK'):
                    self.check()
        self.proof['nativeSdkValidation'] = copy.deepcopy(original)
        self.proof['nativeSdkValidation']['builds'][3] = copy.deepcopy(original['builds'][2])
        with self.assertRaisesRegex(ValueError,'Missing complete SDK'):
            self.check()

    def test_native_proof_cannot_replace_device_or_dependency_checks(self):
        self.macos_record()
        self.proof['devices']['phone']['hardwareAccelerated'] = False
        with self.assertRaisesRegex(ValueError,'Missing physical hardware'):
            self.check()
        self.proof['devices']['phone']['hardwareAccelerated'] = True
        path = 'flutter/bin/cache/artifacts/engine/windows-arm-release/sdk-assembly.json'
        metadata = json.loads(self.members[path]); metadata['sdkDependencyPathsPreflightPassed'] = False
        self.members[path] = json.dumps(metadata).encode(); self.write_archive()
        self.proof['nativeSdkValidation']['sdkArchiveSha256'] = self.proof['sdkArchiveSha256']
        with self.assertRaisesRegex(ValueError,'Missing complete SDK'):
            self.check()


if __name__ == '__main__':
    unittest.main()
