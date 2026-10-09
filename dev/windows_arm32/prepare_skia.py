#!/usr/bin/env python3
"""Prepare Windows OS API selection and native GL discovery in pinned Skia."""
import argparse
from pathlib import Path
from prepare_dependency import prepare_dependency


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path, help='engine/src/flutter/third_party/skia')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    print(prepare_dependency(args.checkout.resolve(), 'skia', args.verify_only))


if __name__ == '__main__':
    main()
