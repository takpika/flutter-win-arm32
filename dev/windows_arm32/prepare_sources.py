#!/usr/bin/env python3
"""Prepare the pinned native dependencies for the shared RT/Phone engine."""
import argparse
from pathlib import Path
from prepare_dependency import prepare_dependency


DEPENDENCIES = {
    'dart': 'dart',
    'angle': 'angle',
    'skia': 'skia',
    'icu': 'icu',
    'boringssl': 'boringssl/src',
    'abseil': 'abseil-cpp',
    'perfetto': 'perfetto',
    'swiftshader': 'swiftshader',
}


def prepare_sources(root, verify_only=False):
    for dependency, directory in DEPENDENCIES.items():
        result = prepare_dependency(root / 'engine/src/flutter/third_party' / directory,
                                    dependency, verify_only)
        print(dependency + ': ' + result, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    prepare_sources(args.root.resolve(), args.verify_only)


if __name__ == '__main__':
    main()
