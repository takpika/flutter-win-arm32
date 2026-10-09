#!/usr/bin/env python3
"""Exercise the actual cross compiler/cache, including VFS/header invalidation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
import shutil
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['rt', 'phone'], required=True)
    parser.add_argument('--toolchain-config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ccache', default='ccache')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    configuration = args.toolchain_config.resolve()
    driver = Path(__file__).resolve().parents[1] / 'toolchain' / (args.platform + '_driver.py')
    with tempfile.TemporaryDirectory(dir=args.output.resolve()) as temporary:
        work = Path(temporary)
        virtual = work / 'virtual'
        virtual.mkdir()
        first, second = work / 'one.h', work / 'two.h'
        first.write_text('#define CACHE_PROOF_VALUE 17\n')
        second.write_text('#define CACHE_PROOF_VALUE 19\n')
        source = work / 'proof.c'
        source.write_text('#include "cAcHe.H"\nint cache_proof(void) { return CACHE_PROOF_VALUE; }\n')
        overlay = work / 'overlay.json'
        def remap(header):
            overlay.write_text(json.dumps({'version': 0, 'case-sensitive': False,
                'use-external-names': False, 'roots': [{'type': 'file',
                'name': str(virtual / 'Cache.h'), 'external-contents': str(header)}]}) + '\n')
        remap(first)
        environment = dict(os.environ,
            FLUTTER_WINDOWS_ARM32_TOOLCHAIN_CONFIG=str(configuration),
            FLUTTER_WINDOWS_ARM32_COMPILER_LAUNCHER=args.ccache,
            CCACHE_DIR=str(work / 'cache'), CCACHE_COMPILERCHECK='content',
            CCACHE_NODIRECT='true', CCACHE_SLOPPINESS='ivfsoverlay',
            CCACHE_LOGFILE=str(work / 'ccache.log'))
        environment.pop('CCACHE_EXTRAFILES', None)
        def stats():
            text = subprocess.check_output([args.ccache, '--print-stats'],
                                           text=True, env=environment)
            result = {}
            for line in text.splitlines():
                fields = line.split()
                if len(fields) == 2 and fields[1].isdecimal():
                    result[fields[0]] = int(fields[1])
            if not {'cache_miss', 'preprocessed_cache_hit'} <= result.keys():
                raise ValueError('Unsupported ccache statistics format')
            print(json.dumps({'cacheStatistics': {key: result[key] for key in
                ('cache_miss', 'preprocessed_cache_hit')}}), flush=True)
            # Preserve the tiny proof's diagnostics even when an assertion fails.
            (args.output / 'latest-statistics.json').write_text(json.dumps(result, indent=2) + '\n')
            if (work / 'ccache.log').exists():
                shutil.copy2(work / 'ccache.log', args.output / 'ccache.log')
            return result
        def compile(cached=True):
            env = dict(environment)
            if not cached: env.pop('FLUTTER_WINDOWS_ARM32_COMPILER_LAUNCHER')
            output = work / 'proof.obj'
            subprocess.run([sys.executable, '-B', str(driver), 'cc', '-c', str(source),
                '-I', str(virtual), '-ivfsoverlay', str(overlay),
                '-MD', '-MF', str(work / 'proof.d'), '-o', str(output)],
                cwd=work, env=env, check=True)
            data = output.read_bytes()
            if data[:2] != b'\xc4\x01': raise ValueError('Expected ARMNT compiler output')
            index = len(list(args.output.glob('proof-*.obj')))
            shutil.copy2(output, args.output / ('proof-' + str(index) + '.obj'))
            if (work / 'ccache.log').exists():
                shutil.copy2(work / 'ccache.log', args.output / 'ccache.log')
            print(json.dumps({'phase': index, 'cached': cached,
                'sha256': hashlib.sha256(data).hexdigest(),
                'coffTimestamp': struct.unpack_from('<I', data, 4)[0]}), flush=True)
            return hashlib.sha256(data).hexdigest()
        baseline = compile(False)
        before = stats()
        # Cross a COFF timestamp boundary so equality is not an accident of
        # compiling the cached and uncached objects in the same second.
        time.sleep(2)
        if compile() != baseline: raise ValueError('Launcher changed compiler output')
        cold = stats()
        if cold['cache_miss'] <= before['cache_miss']: raise ValueError('No cacheable cold compilation')
        if compile() != baseline: raise ValueError('Cached output changed')
        warm = stats()
        if warm['preprocessed_cache_hit'] <= cold['preprocessed_cache_hit']:
            raise ValueError('Identical target compilation did not hit cache')
        first.write_text('#define CACHE_PROOF_VALUE 19\n')
        changed_hash = compile()
        changed = stats()
        if changed_hash == baseline or changed['cache_miss'] <= warm['cache_miss']:
            raise ValueError('Header modification reused stale object')
        remap(second)
        if compile() != changed_hash: raise ValueError('Equivalent mapped header changed code')
        remapped = stats()
        if remapped['cache_miss'] <= changed['cache_miss']:
            raise ValueError('VFS remapping was not included in cache inputs')
        if compile(False) != changed_hash: raise ValueError('Cache and uncached compilation differ')
        report = {'platform': args.platform, 'targetMachine': 'ARMNT',
                  'cachedAndUncachedOutputIdentical': True,
                  'repeatedCompilePreprocessedHit': True,
                  'headerChangeInvalidates': True, 'vfsRemappingInvalidates': True,
                  'statistics': remapped}
        (args.output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'statistics'}))


if __name__ == '__main__':
    main()
