#!/usr/bin/env python3
"""Link Windows ARM32 Rust objects through the SDK's configured native driver."""
import os
import sys
from gnu_driver import main


def link(platform, arguments):
    if platform not in ('rt', 'phone'):
        raise ValueError('Set FLUTTER_WINDOWS_ARM32_PLATFORM to rt or phone')
    # The SDK driver supplies the selected OS family's UCRT contracts. Rust's
    # MSVC target supplies these legacy CRT library selectors as well.
    legacy_crt = {'-l:/defaultlib:msvcrt', '-llegacy_stdio_definitions'}
    sys.argv = [sys.argv[0], 'link',
                *(argument for argument in arguments if argument not in legacy_crt)]
    return main(platform)


if __name__ == '__main__':
    raise SystemExit(link(os.environ.get('FLUTTER_WINDOWS_ARM32_PLATFORM'), sys.argv[1:]))
