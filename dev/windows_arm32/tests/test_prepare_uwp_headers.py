import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prepare_uwp_headers


class HeaderTransferTests(unittest.TestCase):
    def test_success_has_no_retry_or_delay(self):
        command = ['git', '-C', 'owned-source', 'checkout', '--detach', 'FETCH_HEAD']
        result = subprocess.CompletedProcess(command, 0)
        with mock.patch.object(prepare_uwp_headers.subprocess, 'run', return_value=result) as run, \
                mock.patch.object(prepare_uwp_headers.time, 'sleep') as sleep:
            self.assertIs(prepare_uwp_headers.transfer(command), result)
        run.assert_called_once_with(command, check=True, stdout=sys.stderr)
        sleep.assert_not_called()

    def test_lazy_checkout_retries_same_pinned_operation(self):
        command = ['git', '-C', 'owned-source', 'checkout', '--detach', 'FETCH_HEAD']
        failure = subprocess.CalledProcessError(128, command)
        result = subprocess.CompletedProcess(command, 0)
        with mock.patch.object(prepare_uwp_headers.subprocess, 'run',
                               side_effect=[failure, failure, result]) as run, \
                mock.patch.object(prepare_uwp_headers.time, 'sleep') as sleep:
            self.assertIs(prepare_uwp_headers.transfer(command), result)
        self.assertEqual(run.call_args_list,
                         [mock.call(command, check=True, stdout=sys.stderr)] * 3)
        self.assertEqual(sleep.call_args_list, [mock.call(2), mock.call(4)])

    def test_permanent_fetch_failure_remains_failure_and_is_bounded(self):
        command = ['git', '-C', 'owned-source', 'fetch', '--depth', '1',
                   '--filter=blob:none', 'origin', 'a' * 40]
        failure = subprocess.CalledProcessError(128, command)
        with mock.patch.object(prepare_uwp_headers.subprocess, 'run', side_effect=failure) as run, \
                mock.patch.object(prepare_uwp_headers.time, 'sleep') as sleep:
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                prepare_uwp_headers.transfer(command)
        self.assertIs(raised.exception, failure)
        self.assertEqual(run.call_args_list,
                         [mock.call(command, check=True, stdout=sys.stderr)] * 4)
        self.assertEqual(sleep.call_args_list,
                         [mock.call(2), mock.call(4), mock.call(8)])


if __name__ == '__main__':
    unittest.main()
