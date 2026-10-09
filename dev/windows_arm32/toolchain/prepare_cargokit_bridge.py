#!/usr/bin/env python3
"""Generate SDK build glue that retains a package's original Cargokit policy."""
import argparse
from pathlib import Path


def prepare(cargokit, output):
    library = cargokit.resolve() / 'build_tool/lib/src'
    imports = ('builder.dart', 'target.dart', 'environment.dart', 'artifacts_provider.dart')
    for name in imports:
        if not (library / name).is_file():
            raise ValueError('Missing original Cargokit library: ' + str(library / name))
    # Only generated build glue is written. Package policy and source stay intact.
    source = ''.join("import '" + (library / name).as_uri() + "';\n"
                     for name in imports)
    source += """import 'dart:io';

Future<void> main(List<String> arguments) async {
  if (arguments.isEmpty || arguments.length > 2 ||
      (arguments.length == 2 && arguments[1] != '--copy-artifacts') ||
      arguments.first.contains('/') || arguments.first.contains('\\\\')) {
    throw ArgumentError('Supply the prepared SDK Rust target name');
  }
  if (Environment.targetPlatform != 'windows-arm') {
    throw ArgumentError('This SDK build glue is only for windows-arm');
  }
  final environment = BuildEnvironment.fromEnvironment(isAndroid: false);
  final target = Target(rust: arguments.first, flutter: 'windows-arm');
  final builder = RustBuilder(
    target: target,
    environment: environment,
  );
  // SDK Cargo/Rust toolchain dispatch supplies this target and its standard
  // library. Keep the original package's flags and build environment logic.
  final output = await builder.build();
  stdout.writeln('SDK_CARGO_OUTPUT_DIR=$output');
  if (arguments.length == 2) {
    // Use the original package's Windows artifact naming and copy contract.
    final names = getArtifactNames(
      target: target,
      libraryName: environment.crateInfo.packageName,
      aritifactType: AritifactType.dylib,
      remote: false,
    );
    final destination = Directory(Environment.outputDir);
    final libraries = names.where((name) => name.endsWith('.dll')).toList();
    if (libraries.isEmpty || libraries.any((name) =>
        !File('$output/${name}').existsSync())) {
      throw StateError('Cargo did not produce the required Windows DLL');
    }
    destination.createSync(recursive: true);
    for (final name in names) {
      final artifact = File('$output/${name}');
      if (artifact.existsSync()) {
        artifact.copySync('${destination.path}/${name}');
      }
    }
  }
}
"""
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(source)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cargokit-directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.cargokit_directory, args.output))
