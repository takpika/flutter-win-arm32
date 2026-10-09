// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../base/common.dart';
import '../base/file_system.dart';
import '../build_info.dart';
import '../build_system/build_system.dart';
import '../build_system/targets/windows.dart';
import '../cache.dart';
import '../cmake.dart';
import '../cmake_project.dart';
import '../convert.dart';
import '../flutter_plugins.dart';
import '../globals.dart' as globals;
import '../platform_plugins.dart';
import '../plugins.dart';

File get windowsPhoneBuildConfiguration => globals.fs.file(
  globals.fs.path.join(
    Cache.flutterRoot!,
    'bin',
    'cache',
    'artifacts',
    'engine',
    'windows-arm-release',
    'toolchain',
    'phone-build.json',
  ),
);

// The UWP host is built by the SDK's native backend. The desktop unpack step
// supplies Win32 runner headers, DLLs and symbols and is not an input to UWP.
class _ReleaseBundleWindowsPhoneAssets extends ReleaseBundleWindowsAssets {
  const _ReleaseBundleWindowsPhoneAssets() : super(TargetPlatform.windows_arm);

  @override
  String get name => 'release_bundle_windows_phone_assets';

  @override
  List<Target> get dependencies =>
      super.dependencies.where((Target target) => target is! UnpackWindows).toList();
}

Future<void> buildWindowsPhone(
  WindowsProject project,
  BuildInfo buildInfo, {
  required String target,
  bool configOnly = false,
}) async {
  final File configuration = windowsPhoneBuildConfiguration;
  if (!configuration.existsSync()) {
    throwToolExit('This SDK does not have its Windows Phone build artifacts installed.');
  }
  if (buildInfo.mode != BuildMode.release) {
    throwToolExit(
      'Windows Phone ${buildInfo.mode.cliName} runtime artifacts are not installed in this SDK.',
    );
  }
  final Directory output = project.parent.directory
      .childDirectory('build')
      .childDirectory('windows')
      .childDirectory('arm-phone');
  final Directory assets = output.childDirectory('assets');
  if (!configOnly) {
    final BuildResult result = await globals.buildSystem.build(
      const _ReleaseBundleWindowsPhoneAssets(),
      Environment(
        projectDir: project.parent.directory,
        outputDir: assets,
        buildDir: project.parent.directory
            .childDirectory('.dart_tool')
            .childDirectory('flutter_build'),
        defines: <String, String>{
          ...buildInfo.toBuildSystemEnvironment(),
          kTargetPlatform: 'windows-arm',
          kTargetFile: target,
        },
        packageConfigPath: buildInfo.packageConfigPath,
        artifacts: globals.artifacts!,
        fileSystem: globals.fs,
        logger: globals.logger,
        processManager: globals.processManager,
        platform: globals.platform,
        analytics: globals.analytics,
        cacheDir: globals.cache.getRoot(),
        engineVersion: globals.artifacts!.usesLocalArtifacts
            ? null
            : globals.flutterVersion.engineRevision,
        flutterRootDir: globals.fs.directory(Cache.flutterRoot),
        generateDartPluginRegistry: true,
      ),
    );
    if (!result.success) {
      for (final ExceptionMeasurement measurement in result.exceptions.values) {
        globals.logger.printError(
          'Target ${measurement.target} failed: ${measurement.exception}',
          stackTrace: measurement.fatal ? measurement.stackTrace : null,
        );
      }
      throwToolExit('Failed to compile Windows Phone assets.');
    }
  }
  final settings = jsonDecode(configuration.readAsStringSync()) as Map<String, dynamic>;
  final configuredPython = settings['python'] as String;
  final String python = globals.fs.path.isAbsolute(configuredPython)
      ? configuredPython
      : globals.fs.path.normalize(globals.fs.path.join(configuration.parent.path, configuredPython));
  final String appName = project.parent.manifest.appName;
  final Directory runner = project.runnerDirectory;
  final File resource = runner.childFile('Runner.rc');
  final String resourceText = resource.existsSync() ? resource.readAsStringSync() : '';
  final RegExpMatch? title = RegExp(
    r'VALUE\s+"ProductName",\s*"([^"\r\n]*)"',
  ).firstMatch(resourceText);
  final RegExpMatch? icon = RegExp(r'\bICON\s+"([^"\r\n]+)"').firstMatch(resourceText);
  final String displayName = title?.group(1)?.replaceAll(r'\0', '') ?? appName;
  final File iconFile = globals.fs.file(
    globals.fs.path.join(
      runner.path,
      (icon?.group(1) ?? 'resources/app_icon.ico').replaceAll(r'\', '/'),
    ),
  );
  final Map<String, dynamic> defaults =
      settings['packageDefaults'] as Map<String, dynamic>? ?? <String, dynamic>{};
  final String version = (buildInfo.buildName ?? project.parent.manifest.buildName ?? '1.0.0')
      .split('-')
      .first;
  final String revision = buildInfo.buildNumber ?? project.parent.manifest.buildNumber ?? '0';
  output.createSync(recursive: true);
  final File metadata = output.childFile('package-metadata.json');
  metadata.writeAsStringSync(
    jsonEncode(<String, dynamic>{
      'identityName': 'Flutter.${appName.replaceAll('_', '-')}',
      'displayName': displayName,
      'version': '$version.$revision',
      'publisher': defaults['publisher'] ?? 'CN=Flutter Windows ARM Developer',
      'publisherDisplayName': defaults['publisherDisplayName'] ?? 'Flutter Developer',
      'backgroundColor': defaults['backgroundColor'] ?? '#FFFFFF',
      if (defaults['languages'] != null) 'languages': defaults['languages'],
      if (defaults['capabilities'] != null) 'capabilities': defaults['capabilities'],
    }),
  );
  final arguments = <String>[
    python,
    globals.fs.path.join(Cache.flutterRoot!, 'dev', 'windows_arm32', 'build_uwp_bundle.py'),
    '--configuration',
    configuration.path,
    '--application-root',
    project.parent.directory.path,
    '--assets',
    assets.path,
    '--output',
    output.path,
    '--binary-name',
    getCmakeExecutableName(project) ?? project.parent.manifest.appName,
    '--package-metadata',
    metadata.path,
    '--icon',
    iconFile.path,
    if (configOnly) '--config-only',
  ];
  for (final Plugin plugin in await findPlugins(project.parent)) {
    final PluginPlatform? implementation = plugin.platforms['windows'];
    if (implementation is WindowsPlugin &&
        (implementation.hasMethodChannel() || implementation.hasFfi())) {
      if (implementation.hasMethodChannel()) {
        arguments.addAll(<String>['--plugin', plugin.name]);
      }
      if (implementation.hasFfi()) {
        arguments.addAll(<String>['--ffi-plugin', plugin.name]);
      }
      arguments.addAll(<String>['--package', '${plugin.name}=${plugin.path}']);
    }
  }
  await globals.processUtils.run(
    arguments,
    throwOnError: true,
    workingDirectory: project.parent.directory.path,
  );
  globals.logger.printStatus('Built ${globals.fs.path.relative(output.path)}');
}
