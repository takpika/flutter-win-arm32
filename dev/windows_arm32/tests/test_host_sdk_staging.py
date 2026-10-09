import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / 'testing')]
import stage_host_sdk_inputs as staging


class HostSdkStagingTests(unittest.TestCase):
    def archive(self, path, files):
        path.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(path, 'w:gz') as target:
            for name, data in files.items():
                member = tarfile.TarInfo(name); member.size = len(data)
                target.addfile(member, io.BytesIO(data))
        path.with_name(path.name + '.sha256').write_text(hashlib.sha256(path.read_bytes()).hexdigest())

    def fixture(self, work, host):
        inputs = work / 'inputs'
        for component, filename in (
                ('compiler', 'windows-arm32-compiler-' + host), ('python', 'python-' + host),
                ('msix', 'windows-arm32-msix-' + host),
                ('rust-1.95.0', 'windows-arm32-rust-1.95.0-' + host),
                ('rust-1.93.1', 'windows-arm32-rust-1.93.1-' + host),
                ('engines', 'windows-arm32-engines-linux-x64')):
            self.archive(inputs / component / (filename + '.tar.gz'), {'component': component.encode()})
        if host == 'windows-x64':
            self.archive(inputs / 'resources/windows-arm32-resource-tools-windows-x64.tar.gz', {'component': b'resources'})
            launcher = inputs / 'launcher/launcher'
            launcher.mkdir(parents=True)
            for name in ('sdk-python-launcher.exe', 'sdk-python-launcher.provenance.json'):
                (launcher / name).write_bytes(b'original-launcher')
        else:
            self.archive(inputs / 'generator' / ('windows-arm32-snapshot-' + host + '.tar.gz'),
                         {'gen_snapshot': b'original-macos-generator'})
        prefix = 'flutter/bin/cache/windows-arm32-dependencies/'
        gnu = 'llvm-mingw-20251104-ucrt-ubuntu-22.04-x86_64/'
        files = {'cppwinrt/winrt/base.h': b'projection', 'atl/include/atlbase.h': b'atl',
                 'rust-dependencies/.cargo/sdk-patches.toml': b'patches',
                 'windows-sdk/include/windows.h': b'previous shim',
                 'windows-sdk/sdk-provenance.json': b'{"extractedSourceHashes":{}}',
                 gnu + 'generic-w64-mingw32/include/header.h': b'generic',
                 gnu + 'armv7-w64-mingw32/lib/libtarget.a': b'original-target-library',
                 gnu + 'lib/clang/22/lib/windows/libclang_rt.builtins-arm.a': b'original-builtins',
                 gnu + 'bin/clang': b'must-not-stage-linux-compiler'}
        self.archive(inputs / 'linux-sdk/flutter-windows-arm32-linux-x64.tar.gz',
                     {prefix + name: data for name, data in files.items()})

    def test_windows_and_both_mac_hosts_share_target_bytes_without_foreign_host_tools(self):
        for host in ('windows-x64', 'macos-x64', 'macos-arm64'):
            with self.subTest(host=host), tempfile.TemporaryDirectory() as scratch:
                work = Path(scratch); sdk = work / 'sdk'; sdk.mkdir()
                self.fixture(work, host)
                with mock.patch.object(staging, 'install_resource_tools', return_value={}) as resources:
                    staging.prepare(work, sdk, host)
                deps = sdk / 'bin/cache/windows-arm32-dependencies'
                self.assertEqual(resources.call_count, int(host == 'windows-x64'))
                self.assertFalse((deps / 'target-sysroot/bin').exists())
                self.assertEqual((deps / 'target-sysroot/armv7-w64-mingw32/lib/libtarget.a').read_bytes(),
                                 b'original-target-library')
                self.assertEqual((deps / 'compiler/component').read_bytes(), b'compiler')
                self.assertEqual((deps / 'windows-sdk/include/windows.h').read_bytes(),
                                 (Path(staging.__file__).resolve().parents[1] / 'toolchain/sdk/windows.h').read_bytes())
                if host != 'windows-x64':
                    self.assertEqual((work / 'generator/gen_snapshot').read_bytes(), b'original-macos-generator')
                    self.assertFalse((deps / 'launcher').exists())


if __name__ == '__main__':
    unittest.main()
