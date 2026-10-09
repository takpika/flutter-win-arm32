"""A validation-branch build can be promoted only with the identical source."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_sdk_set import verify_source_build


class BuildOriginTest(unittest.TestCase):
    def setUp(self):
        self.run = {'status':'completed','conclusion':'success','event':'workflow_dispatch',
            'head_repository':{'full_name':'owner/port'},'head_sha':'a'*40,
            'head_branch':'sdk-host-tools-validation',
            'path':'.github/workflows/windows-arm32-engine.yml'}

    def test_identical_promoted_commit_needs_no_rebuild_for_branch_rename(self):
        verify_source_build(self.run, 'owner/port', 'a'*40, 'linux-x64')

    def test_public_branch_label_does_not_accept_an_older_commit(self):
        self.run['head_branch'] = 'windows-arm32-3.38.10'
        with self.assertRaises(ValueError):
            verify_source_build(self.run, 'owner/port', 'b'*40, 'linux-x64')

    def test_reject_other_producer_repository_or_execution_event(self):
        for changes in ({'path':'.github/workflows/other.yml'},
                        {'head_repository':{'full_name':'fork/port'}},
                        {'event':'pull_request_target'}, {'status':'in_progress'},
                        {'conclusion':'failure'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                run = copy.deepcopy(self.run);run.update(changes)
                verify_source_build(run, 'owner/port', 'a'*40, 'linux-x64')

    def test_each_host_requires_its_actual_sdk_workflow(self):
        for host, workflow in (('windows-x64','windows-arm32-windows-sdk.yml'),
                               ('macos-x64','windows-arm32-macos-sdk.yml'),
                               ('macos-arm64','windows-arm32-macos-sdk.yml')):
            with self.subTest(host=host):
                run = copy.deepcopy(self.run);run['path'] = '.github/workflows/'+workflow
                verify_source_build(run, 'owner/port', 'a'*40, host)
                with self.assertRaises(ValueError):
                    verify_source_build(self.run, 'owner/port', 'a'*40, host)
