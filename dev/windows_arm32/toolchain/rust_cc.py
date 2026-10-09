#!/usr/bin/env python3
"""Select the native language for cc-rs invocations without rewriting sources."""
import argparse
from pathlib import Path
import sys
from gnu_driver import main

if __name__ == '__main__':
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--sdk-platform', choices=('rt', 'phone'), required=True)
    options, arguments = parser.parse_known_args()
    assembly = any(Path(value).suffix.lower() in ('.s', '.asm') for value in arguments)
    mode = 'asm' if assembly else 'cc'
    sys.argv = [sys.argv[0], mode, *arguments]
    raise SystemExit(main(options.sdk_platform))
