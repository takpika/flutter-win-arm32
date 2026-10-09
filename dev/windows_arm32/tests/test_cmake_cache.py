import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / 'toolchain/cmake_cache.py'
spec = importlib.util.spec_from_file_location('cmake_cache', MODULE)
cache = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache)


class CmakeCacheTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build'
        self.sdk = self.root / 'old SDK'
        self.sdk.mkdir()
        self.toolchain = self.sdk / 'rt-cross.cmake'
        self.toolchain.write_text(
            'set(CACHE_GUARD_ORIGIN "${CMAKE_CURRENT_LIST_DIR}")\n')

    def test_same_sdk_preserves_incremental_cache(self):
        cache.prepare(self.build, self.toolchain)
        (self.build / 'CMakeCache.txt').write_text('existing cache')
        information = self.build / 'CMakeFiles'
        information.mkdir()
        (information / 'compiled.obj').write_bytes(b'compiled object')
        self.assertFalse(cache.prepare(self.build, self.toolchain))
        self.assertEqual((information / 'compiled.obj').read_bytes(), b'compiled object')
        self.assertEqual((self.build / 'CMakeCache.txt').read_text(), 'existing cache')

    def test_unstamped_legacy_metadata_is_invalidated_once(self):
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text('CMAKE_TOOLCHAIN_FILE:FILEPATH=new')
        information = self.build / 'CMakeFiles'
        information.mkdir()
        (information / 'CMakeSystem.cmake').write_text('include(old-sdk-toolchain)')
        output = self.build / 'unrelated-output.bin'
        output.write_bytes(b'preserved output')
        self.assertTrue(cache.prepare(self.build, self.toolchain))
        self.assertFalse(information.exists())
        self.assertFalse((self.build / 'CMakeCache.txt').exists())
        self.assertEqual(output.read_bytes(), b'preserved output')
        self.assertFalse(cache.prepare(self.build, self.toolchain))

    @unittest.skipIf(os.name == 'nt', 'Windows launchers execute in the SDK assembly test')
    def test_generated_cache_launcher_uses_sdk_relative_configuration(self):
        sys.path.insert(0, str(MODULE.parent))
        self.addCleanup(sys.path.remove, str(MODULE.parent))
        from prepare_cmake import prepare
        dependencies = self.root / 'dependencies'
        dependencies.mkdir()
        compiler = dependencies / 'clang'
        compiler.touch()
        cppwinrt = dependencies / 'cppwinrt'
        (cppwinrt / 'winrt').mkdir(parents=True)
        (cppwinrt / 'winrt/base.h').touch()
        configuration = dependencies / 'toolchain.json'
        configuration.write_text(json.dumps({
            'compiler': str(compiler), 'llvmBin': str(dependencies),
            'sysroot': str(dependencies), 'resourceDir': str(dependencies),
        }))
        output = self.root / 'ツール chain'
        prepare(output, configuration, cppwinrt, python=Path(sys.executable))
        metadata = json.loads((output / 'cross-build.json').read_text())
        subprocess.run([str(output / metadata['cachePreflight']),
                        '--build-directory', str(self.build),
                        '--toolchain-file', str(output / 'rt-cross.cmake')], check=True)
        self.assertEqual((self.build / '.flutter-windows-arm32-toolchain').read_text(),
                         (output / 'rt-cross.cmake').resolve().as_posix() + '\n')

    @unittest.skipUnless(shutil.which('cmake'), 'CMake is required for relocation execution')
    def test_actual_cmake_after_old_sdk_path_disappears(self):
        source = self.root / 'project'
        source.mkdir()
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(cache_guard LANGUAGES NONE)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/origin.txt" "${CACHE_GUARD_ORIGIN}")\n')

        def configure(toolchain):
            subprocess.run(['cmake', '-S', str(source), '-B', str(self.build),
                            '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain)],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        cache.prepare(self.build, self.toolchain)
        configure(self.toolchain)
        relocated = self.root / '移動先 SDK'
        self.sdk.rename(relocated)
        self.assertFalse(self.sdk.exists())
        toolchain = relocated / self.toolchain.name
        self.assertTrue(cache.prepare(self.build, toolchain))
        configure(toolchain)
        self.assertEqual((self.build / 'origin.txt').read_text(), relocated.as_posix())
        self.assertFalse(cache.prepare(self.build, toolchain))


if __name__ == '__main__':
    unittest.main()
