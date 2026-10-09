// Copyright 2014 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

import '../base/analyze_size.dart';
import '../base/common.dart';
import '../base/os.dart';
import '../build_info.dart';
import '../cache.dart';
import '../features.dart';
import '../globals.dart' as globals;
import '../runner/flutter_command.dart' show FlutterCommandResult;
import '../windows/build_windows.dart';
import '../windows/build_windows_phone.dart';
import '../windows/visual_studio.dart';
import 'build.dart';

/// A command to build a windows desktop target through a build shell script.
class BuildWindowsCommand extends BuildSubCommand {
  BuildWindowsCommand({
    required super.logger,
    required OperatingSystemUtils operatingSystemUtils,
    bool verboseHelp = false,
  }) : _operatingSystemUtils = operatingSystemUtils,
       super(verboseHelp: verboseHelp) {
    addCommonDesktopBuildOptions(verboseHelp: verboseHelp);
    argParser.addOption(
      'target-platform',
      allowed: <String>['windows-x64', 'windows-arm64', 'windows-arm'],
      help: 'The Windows architecture to build for.',
    );
    argParser.addOption(
      'windows-family',
      allowed: <String>['desktop', 'phone'],
      defaultsTo: 'desktop',
      help: 'The Windows application family to build for.',
    );
    argParser.addFlag(
      'config-only',
      help: 'Update the project configuration without performing a build.',
    );
  }

  final OperatingSystemUtils _operatingSystemUtils;

  @override
  final name = 'windows';

  @override
  bool get hidden =>
      (!featureFlags.isWindowsEnabled || !globals.platform.isWindows) &&
      !windowsArmCrossBuildAvailable;

  @override
  Future<Set<DevelopmentArtifact>> get requiredArtifacts async => <DevelopmentArtifact>{
    DevelopmentArtifact.windows,
  };

  @override
  String get description => 'Build a Windows desktop application.';

  @visibleForTesting
  VisualStudio? visualStudioOverride;

  bool get configOnly => boolArg('config-only');

  @override
  Future<FlutterCommandResult> runCommand() async {
    final BuildInfo buildInfo = await getBuildInfo();
    final phone = stringArg('windows-family') == 'phone';
    final defaultTargetPlatform = phone
        ? 'windows-arm'
        : !globals.platform.isWindows && windowsArmCrossBuildAvailable
        ? 'windows-arm'
        : (_operatingSystemUtils.hostPlatform == HostPlatform.windows_arm64)
        ? 'windows-arm64'
        : 'windows-x64';
    final TargetPlatform targetPlatform = getTargetPlatformForName(
      stringArg('target-platform') ?? defaultTargetPlatform,
    );
    final bool crossBuild =
        targetPlatform == TargetPlatform.windows_arm &&
        !globals.platform.isWindows &&
        windowsArmCrossBuildAvailable;
    if (!featureFlags.isWindowsEnabled && !crossBuild) {
      throwToolExit(
        '"build windows" is not currently supported. To enable, run "flutter config --enable-windows-desktop".',
      );
    }
    if (!globals.platform.isWindows && !crossBuild) {
      throwToolExit('"build windows" only supported on Windows hosts.');
    }

    if (phone) {
      if (targetPlatform != TargetPlatform.windows_arm) {
        throwToolExit('The Windows Phone SDK targets windows-arm.');
      }
      await buildWindowsPhone(
        project.windows,
        buildInfo,
        target: targetFile,
        configOnly: configOnly,
      );
      return FlutterCommandResult.success();
    }

    await buildWindows(
      project.windows,
      buildInfo,
      targetPlatform,
      target: targetFile,
      visualStudioOverride: visualStudioOverride,
      sizeAnalyzer: SizeAnalyzer(
        fileSystem: globals.fs,
        logger: globals.logger,
        appFilenamePattern: 'app.so',
        analytics: analytics,
      ),
      configOnly: configOnly,
    );
    return FlutterCommandResult.success();
  }
}
