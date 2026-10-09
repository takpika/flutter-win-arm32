#!/usr/bin/env python3
"""Apply the shared Windows ARM32 port to the pinned ANGLE checkout."""
import argparse
from pathlib import Path
from prepare_dependency import prepare_dependency


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path, help='engine/src/flutter/third_party/angle')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    print(prepare_dependency(args.checkout.resolve(), 'angle', args.verify_only))


if __name__ == '__main__':
    main()
