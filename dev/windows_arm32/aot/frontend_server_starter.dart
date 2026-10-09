// Use the frontend compiler built from the same Dart SDK sources as the
// Windows ARM runtime. The Flutter compiler protocol passes through unchanged.
import 'dart:io';

Future<void> main(List<String> arguments) async {
  final snapshot = File.fromUri(Platform.script.resolve('frontend_server_aot.dart.snapshot'));
  final runtime = File.fromUri(File(Platform.resolvedExecutable).parent.uri.resolve(
    'dartaotruntime${Platform.isWindows ? '.exe' : ''}',
  ));
  final process = await Process.start(runtime.path, [snapshot.path, ...arguments],
      mode: ProcessStartMode.inheritStdio);
  exit(await process.exitCode);
}
