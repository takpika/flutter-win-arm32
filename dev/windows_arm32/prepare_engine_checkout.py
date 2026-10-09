#!/usr/bin/env python3
"""Prepare pinned engine dependencies using Flutter's upstream gclient setup."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def prepare_angle_astc(root, environment):
    # Flutter does not recurse into ANGLE's DEPS. Standalone ANGLE enables
    # ASTC, so obtain that dependency from ANGLE's own versioned declaration.
    angle = root / 'engine/src/flutter/third_party/angle'
    parsed = ast.parse((angle / 'DEPS').read_text())
    dictionaries = {node.targets[0].id: node.value for node in parsed.body
                    if isinstance(node, ast.Assign) and len(node.targets) == 1
                    and isinstance(node.targets[0], ast.Name)
                    and isinstance(node.value, ast.Dict)}
    def entry(dictionary, name):
        return next(value for key, value in zip(dictionary.keys, dictionary.values)
                    if isinstance(key, ast.Constant) and key.value == name)
    chromium_git = ast.literal_eval(entry(dictionaries['vars'], 'chromium_git'))
    dependency = ast.literal_eval(entry(dictionaries['deps'], 'third_party/astc-encoder/src'))
    url, revision = dependency['url'].format(chromium_git=chromium_git).rsplit('@', 1)
    if not re.fullmatch('[0-9a-f]{40}', revision) or not url.startswith('https://'):
        raise ValueError('ANGLE ASTC dependency must specify a pinned HTTPS revision')
    destination = angle / 'third_party/astc-encoder/src'
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'init', str(destination)], env=environment, check=True)
        subprocess.run(['git', 'fetch', '--depth=1', url, revision], cwd=destination,
                       env=environment, check=True)
        subprocess.run(['git', 'checkout', '--detach', revision], cwd=destination,
                       env=environment, check=True)
    # Never reset or overwrite an existing dependency checkout.
    if not (destination / '.git').exists():
        raise ValueError('Existing ASTC sources are not a verifiable Git checkout')
    current = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=destination,
                                      env=environment, text=True).strip()
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=destination,
                                    env=environment, text=True).strip()
    if current != revision or dirty:
        raise ValueError('Existing ASTC checkout differs from the pinned clean revision')
    sources = re.findall(r'"(src/Source/[^"\n]+)"',
                         (angle / 'third_party/astc-encoder/BUILD.gn').read_text())
    if not sources or any(not (angle / 'third_party/astc-encoder' / source).is_file()
                          for source in sources):
        raise ValueError('ANGLE ASTC build inputs are incomplete')
    return {'revision': revision, 'url': url, 'verifiedSourceFiles': len(sources)}


def prepare(root, gclient, configure_only=False, windows_inputs=False):
    root = root.resolve()
    template = root / 'engine/scripts/standard.gclient'
    if not template.is_file() or not (root / 'DEPS').is_file():
        raise ValueError('A complete Flutter monorepo checkout is required')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    configuration = root / '.gclient'
    original_template = template.read_bytes()
    expected = original_template
    if windows_inputs:
        parsed = ast.parse(expected.decode('utf-8'))
        assignment = next(node for node in parsed.body if isinstance(node, ast.Assign)
                          and any(isinstance(target, ast.Name) and target.id == 'solutions'
                                  for target in node.targets))
        solutions = ast.literal_eval(assignment.value)
        # Fetch inputs for this cross build only. This changes no GN feature
        # flags and does not alter another checkout's dependency configuration.
        solutions[0]['custom_vars'] = {
            'download_android_deps': False, 'download_jdk': False,
            'download_linux_deps': False, 'download_fuchsia_deps': False,
        }
        expected = ('# Isolated Windows ARM32 CI dependency selection.\nsolutions = '
                    + repr(solutions) + '\n').encode('utf-8')
    # Do not replace another project's dependency policy or reset its checkout.
    if configuration.exists() and configuration.read_bytes() != expected:
        raise ValueError('Existing .gclient differs from the upstream template; retain it and sync explicitly')
    if not configuration.exists():
        configuration.write_bytes(expected)
    scratch = root / '.windows-arm32-cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, CI='true', TMPDIR=str(scratch), TMP=str(scratch),
                       TEMP=str(scratch),
                       CIPD_CACHE_DIR=str(root / '.windows-arm32-cache/cipd'),
                       PUB_CACHE=str(root / '.windows-arm32-cache/pub'),
                       XDG_CONFIG_HOME=str(root / '.windows-arm32-cache/config'))
    astc = None
    if not configure_only:
        # managed=False in the upstream template preserves the root checkout.
        # DEPS selects dependency revisions; no force/reset flags are used.
        subprocess.run([gclient, 'sync', '--no-history'], cwd=root, env=environment, check=True)
        current = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        if current != revision:
            raise ValueError('Dependency synchronization changed the root revision')
        if windows_inputs:
            astc = prepare_angle_astc(root, environment)
    report = {'frameworkRevision': revision,
              'depsSha256': hashlib.sha256((root / 'DEPS').read_bytes()).hexdigest(),
              'upstreamGclientTemplateSha256': hashlib.sha256(original_template).hexdigest(),
              'gclientConfigurationSha256': hashlib.sha256(expected).hexdigest(),
              'dependencySyncCompleted': not configure_only,
              'windowsCrossBuildInputsSelected': windows_inputs,
              'rootRevisionUnchanged': True}
    if astc is not None:
        report['angleAstcDependency'] = astc
    destination = root / '.windows-arm32-cache/checkout-preparation.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--gclient', default='gclient')
    parser.add_argument('--configure-only', action='store_true',
                        help='Write the upstream setup without fetching dependencies')
    parser.add_argument('--windows-inputs', action='store_true',
                        help='In an isolated checkout, omit unrelated platform SDK downloads')
    args = parser.parse_args()
    print(prepare(args.root, args.gclient, args.configure_only, args.windows_inputs))
