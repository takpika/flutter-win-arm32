"""Invalidate generated CMake metadata when its SDK toolchain moves."""
import argparse
from pathlib import Path
import shutil


def prepare(build_directory, toolchain):
    toolchain = toolchain.resolve(strict=True)
    if not toolchain.is_file():
        raise ValueError('Missing SDK CMake toolchain')
    build_directory.mkdir(parents=True, exist_ok=True)
    marker = build_directory / '.flutter-windows-arm32-toolchain'
    if marker.is_symlink():
        raise ValueError('Generated toolchain marker must not be a symlink')
    identity = toolchain.as_posix() + '\n'
    if marker.exists() and marker.read_text(encoding='utf-8') == identity:
        return False
    cache = build_directory / 'CMakeCache.txt'
    information = build_directory / 'CMakeFiles'
    changed = cache.exists() or information.exists()
    if cache.exists() or cache.is_symlink():
        cache.unlink()
    if information.is_symlink():
        information.unlink()
    elif information.exists():
        shutil.rmtree(information)
    marker.write_text(identity, encoding='utf-8')
    return changed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    parser.add_argument('--toolchain-file', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.build_directory, args.toolchain_file)
