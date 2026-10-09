#!/usr/bin/env python3
"""Use the shared compiler compatibility layer for Windows x64 host tools."""
from gnu_driver import main

if __name__ == '__main__':
    raise SystemExit(main('rt', target_cpu='x64'))
