from pathlib import Path
import shlex
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import configure_engine


class WindowsAngleConfigurationTests(unittest.TestCase):
    def test_cross_hosts_keep_windows_context_switch_protection(self):
        # Flutter's normal GN entry point gates this Windows crash workaround
        # on the build host. Both ARM32 runtime families must retain it when
        # their compiler runs in an Ubuntu container or on macOS.
        for host in ('Linux', 'Darwin', 'Windows'):
            for target in ('rt', 'phone'):
                with self.subTest(host=host, target=target), mock.patch.object(
                        configure_engine.host_platform, 'system', return_value=host):
                    _, command = configure_engine.command(target, Path('test-output'))
                    settings = shlex.split(next(argument.partition('=')[2]
                                                for argument in command
                                                if argument.startswith('--gn-args=')))
                    self.assertIn('--target-os=win', command)
                    self.assertIn('angle_force_context_check_every_call=true', settings)


if __name__ == '__main__':
    unittest.main()
