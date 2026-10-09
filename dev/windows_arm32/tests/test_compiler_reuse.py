import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ci_cache
import reuse_compiler
from toolchain.package_compiler import TOOLS


class CompilerReuseTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.archive = self.root / 'compiler.tar.gz'
        self.manifest = {'upstreamRevision': 'a' * 40, 'patches': []}

    def write_archive(self, *, revision=None, patches=None, host='x86_64',
                      missing=None, extra=None, alias=None, tamper=False, duplicate=False):
        sha = lambda data: hashlib.sha256(data).hexdigest()
        payload = {'bin/clang': b'compiled-tool', 'lib/libLTO.so': b'lto',
                   'lib/clang/22/include/stddef.h': b'header',
                   'LLVM-LICENSE.txt': b'license'}
        report = {'sourceBuilt': True, 'compilerSha256': sha(payload['bin/clang']),
                  'revision': revision or self.manifest['upstreamRevision'],
                  'patches': patches or []}
        payload['source-provenance.json'] = json.dumps(report).encode()
        aliases = {'bin/' + name: 'clang' for name in TOOLS if name != 'clang'}
        if alias:
            aliases['bin/lld'] = alias
        files = {name: {'sha256': sha(data), 'bytes': len(data)} for name, data in payload.items()}
        files.update({name: {'symlink': target} for name, target in aliases.items()})
        if missing:
            files.pop(missing); aliases.pop(missing, None); payload.pop(missing, None)
        payload['distribution-manifest.json'] = json.dumps({
            'hostArchitecture': host, 'systemLibrariesCopied': False, 'files': files}).encode()
        if extra:
            payload[extra] = b'unknown'
        if tamper:
            payload['lib/libLTO.so'] = b'changed'
        with tarfile.open(self.archive, 'w:gz') as target:
            for name, data in payload.items():
                member = tarfile.TarInfo(name); member.size = len(data)
                target.addfile(member, io.BytesIO(data))
            for name, link in aliases.items():
                member = tarfile.TarInfo(name); member.type = tarfile.SYMTYPE; member.linkname = link
                target.addfile(member)
            if duplicate:
                member = tarfile.TarInfo('bin/clang'); member.size = len(payload['bin/clang'])
                target.addfile(member, io.BytesIO(payload['bin/clang']))
        self.archive.with_name(self.archive.name + '.sha256').write_text(sha(self.archive.read_bytes()))

    def test_complete_source_identical_distribution_is_accepted(self):
        self.write_archive()
        report = ci_cache.verify_compiler_archive(self.archive, self.manifest)
        self.assertTrue(report['sourceBuilt'])

    def test_source_patch_payload_architecture_and_member_failures_are_rejected(self):
        for options in ({'revision': 'b' * 40}, {'patches': [{'patch': 'changed', 'sha256': 'f' * 64}]},
                        {'tamper': True}, {'host': 'aarch64'}, {'missing': 'bin/llvm-mc'},
                        {'missing': 'lib/clang/22/include/stddef.h'},
                        {'extra': 'bin/unknown'}, {'extra': '../outside'},
                        {'alias': '../../outside'}, {'duplicate': True}):
            with self.subTest(options=options):
                self.write_archive(**options)
                with self.assertRaises(ValueError):
                    ci_cache.verify_compiler_archive(self.archive, self.manifest)

    def test_archive_checksum_is_required(self):
        self.write_archive()
        self.archive.with_name(self.archive.name + '.sha256').write_text('0' * 64)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            ci_cache.verify_compiler_archive(self.archive, self.manifest)

    def test_deployment_creates_missing_work_parent_and_preserves_provenance(self):
        self.write_archive()
        workflow = self.root / '.github/workflows/windows-arm32-compiler.yml'
        workflow.parent.mkdir(parents=True)
        script = ''.join('      - name: ' + name + '\n        run: |\n          build\n'
                         for name in ('Build compiler and LLVM tools from pinned source',
                                      'Package source-built tools and license'))
        workflow.write_text(script)
        run = {'status': 'completed', 'conclusion': 'success', 'event': 'workflow_dispatch',
               'head_repository': {'full_name': 'owner/project'}, 'head_sha': 'a' * 40,
               'path': '.github/workflows/windows-arm32-engine.yml'}

        def output(command, **options):
            return json.dumps(run) if command[0] == 'gh' else script

        def execute(command, **options):
            if command[0] == 'gh':
                inputs = Path(command[-1]); inputs.mkdir()
                archive = inputs / 'windows-arm32-compiler-linux-x64.tar.gz'
                shutil.copy2(self.archive, archive)
                archive.with_name(archive.name + '.sha256').write_text(
                    hashlib.sha256(archive.read_bytes()).hexdigest())

        with mock.patch.dict(os.environ, GITHUB_REPOSITORY='owner/project'), \
                mock.patch.object(sys, 'argv', ['reuse_compiler', '--run', '123', '--work', '_work/reuse']), \
                mock.patch.object(reuse_compiler, 'producer_files', return_value=[]), \
                mock.patch.object(reuse_compiler.subprocess, 'check_output', side_effect=output), \
                mock.patch.object(reuse_compiler.subprocess, 'run', side_effect=execute), \
                mock.patch.object(reuse_compiler, 'verify_compiler_archive',
                                  side_effect=lambda archive: ci_cache.verify_compiler_archive(archive, self.manifest)):
            previous = Path.cwd()
            try:
                os.chdir(self.root)
                reuse_compiler.main()
            finally:
                os.chdir(previous)
        self.assertTrue((self.root / '_work/compiler/bin/clang').is_file())
        report = json.loads((self.root / '_work/compiler-reuse.json').read_text())
        self.assertEqual(report['sourceRun'], 123)
        self.assertTrue(report['originalSourceProvenanceRetained'])
        self.assertTrue(report['currentHostCompatibilityTestsRequired'])
        self.assertFalse(report['rebuiltWithCurrentHostPackages'])

    def test_exact_cache_still_rejects_different_host_inputs(self):
        self.write_archive()
        expected, stored = self.root / 'expected.json', self.root / 'stored.json'
        expected.write_text(json.dumps({'kind': 'compiler', 'hostPackagesSha256': 'current'}))
        stored.write_text(json.dumps({'kind': 'compiler', 'hostPackagesSha256': 'previous'}))
        with self.assertRaisesRegex(ValueError, 'native inputs differ'):
            ci_cache.verify(expected, stored, self.archive)

    def test_only_successful_same_repository_source_build_runs_are_accepted(self):
        good = {'status': 'completed', 'conclusion': 'success', 'event': 'workflow_dispatch',
                'head_repository': {'full_name': 'owner/project'}, 'head_sha': 'a' * 40,
                'path': '.github/workflows/windows-arm32-engine.yml'}
        self.assertEqual(reuse_compiler.validate_run(good, 'owner/project'), 'a' * 40)
        for field, value in (('status', 'in_progress'), ('conclusion', 'failure'),
                             ('event', 'pull_request'), ('head_sha', 'not-a-sha'),
                             ('head_repository', {'full_name': 'fork/project'}),
                             ('path', '.github/workflows/unrelated.yml')):
            with self.subTest(field=field):
                bad = copy.deepcopy(good); bad[field] = value
                with self.assertRaises(ValueError):
                    reuse_compiler.validate_run(bad, 'owner/project')

    def test_compiler_build_command_comparison_keeps_the_script(self):
        original = '      - name: Build\n        if: true\n        run: |\n          compile --full\n      - name: Check\n'
        same_script = original.replace('if: true', 'if: reused != true')
        self.assertEqual(reuse_compiler.producer_step(original, 'Build'),
                         reuse_compiler.producer_step(same_script, 'Build'))
        changed = original.replace('--full', '--different')
        self.assertNotEqual(reuse_compiler.producer_step(original, 'Build'),
                            reuse_compiler.producer_step(changed, 'Build'))


if __name__ == '__main__':
    unittest.main()
