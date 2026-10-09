// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';
import 'dart:ui';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_core_platform_interface/firebase_core_platform_interface.dart'
    as core;
import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import '../../../firebase_messaging.dart';

const options = FirebaseOptions(
    apiKey: 'synthetic-public-key',
    appId: 'windows-core',
    messagingSenderId: '123',
    projectId: 'synthetic');

class Core extends core.FirebasePlatform {
  int initializes = 0;
  final value = core.FirebaseAppPlatform('[DEFAULT]', options);
  @override
  List<core.FirebaseAppPlatform> get apps => [value];
  @override
  core.FirebaseAppPlatform app([String name = '[DEFAULT]']) => value;
  @override
  Future<core.FirebaseAppPlatform> initializeApp(
      {String? name, FirebaseOptions? options}) async {
    initializes++;
    return value;
  }
}

Completer<void>? backgroundGate;
final backgroundMessages = <String?>[];
@pragma('vm:entry-point')
Future<void> background(RemoteMessage message) async {
  backgroundMessages.add(message.messageId);
  await backgroundGate!.future;
}

void main() {
  final binding = TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('plugins.flutter.io/firebase_messaging');
  const codec = StandardMethodCodec();
  int? rawHandle;
  Future<dynamic> incoming(String name, dynamic arguments,
      {String target = 'plugins.flutter.io/firebase_messaging'}) async {
    final response = Completer<ByteData?>();
    binding.defaultBinaryMessenger.handlePlatformMessage(target,
        codec.encodeMethodCall(MethodCall(name, arguments)), response.complete);
    return codec.decodeEnvelope((await response.future)!);
  }

  test(
      'original public API delegates policy and pending Futures to the SDK channel',
      () async {
    final calls = <MethodCall>[];
    final initialized = Completer<void>();
    Completer<void>? topic;
    bool failToken = false, consumedInitial = false;
    final settings = <String, dynamic>{
      'authorizationStatus': 1,
      for (final key in [
        'alert',
        'announcement',
        'badge',
        'carPlay',
        'lockScreen',
        'notificationCenter',
        'sound',
        'timeSensitive',
        'criticalAlert',
        'providesAppNotificationSettings',
        'showPreviews'
      ])
        key: -1
    };
    binding.defaultBinaryMessenger.setMockMethodCallHandler(channel,
        (call) async {
      calls.add(call);
      switch (call.method) {
        case 'Messaging#initialized':
          await initialized.future;
          return null;
        case 'Messaging#startBackgroundIsolate':
          rawHandle = (call.arguments as Map)['userCallbackHandle'] as int;
          return null;
        case 'Messaging#getToken':
          if (failToken) throw PlatformException(code: 'sdk-native-error');
          return {'token': 'synthetic-native-token'};
        case 'Messaging#deleteToken':
          return null;
        case 'Messaging#getInitialMessage':
          if (consumedInitial) return null;
          consumedInitial = true;
          return {
            'messageId': 'cold',
            'data': {'日本語': '通知'}
          };
        case 'Messaging#subscribeToTopic':
          await topic!.future;
          return null;
        case 'Messaging#unsubscribeFromTopic':
          return null;
        case 'Messaging#setAutoInitEnabled':
          return {'isAutoInitEnabled': false};
        case 'Messaging#setDeliveryMetricsExportToBigQuery':
          return null;
        case 'Messaging#getNotificationSettings':
        case 'Messaging#requestPermission':
          return settings;
        case 'Messaging#isSupported':
          return true;
        default:
          throw StateError('Unexpected native method ${call.method}');
      }
    });
    final platform = Core();
    core.FirebasePlatform.instance = platform;
    Firebase.delegatePackingProperty = null;
    WindowsPhoneFirebaseMessaging.registerWith(
        dispatcherLibraryUri: 'package:bridge/dispatcher.dart');
    final app = Firebase.initializeApp(options: options);
    await Future<void>.delayed(Duration.zero);
    expect(platform.initializes, 0);
    initialized.complete();
    await app;
    expect(platform.initializes, 1);
    final messaging = FirebaseMessaging.instance;
    expect(await messaging.isSupported(), true);
    expect(await messaging.getToken(), 'synthetic-native-token');
    expect(calls.last.arguments, {'appName': '[DEFAULT]'});
    expect((await messaging.getInitialMessage())!.data, {'日本語': '通知'});
    expect(await messaging.getInitialMessage(), null);
    await messaging.deleteToken();
    final beforeTopic = calls.length;
    topic = Completer<void>();
    bool topicDone = false;
    final subscribing =
        messaging.subscribeToTopic('sdk-native-policy').then((_) {
      topicDone = true;
    });
    await Future<void>.delayed(Duration.zero);
    expect(topicDone, false);
    expect(calls.sublist(beforeTopic).map((c) => c.method).toList(),
        ['Messaging#subscribeToTopic']);
    expect(calls.last.arguments,
        {'appName': '[DEFAULT]', 'topic': 'sdk-native-policy'});
    topic.complete();
    await subscribing;
    expect(topicDone, true);
    await messaging.unsubscribeFromTopic('sdk-native-policy');
    await messaging.setAutoInitEnabled(true);
    expect(messaging.isAutoInitEnabled, false);
    await messaging.setDeliveryMetricsExportToBigQuery(true);
    expect(calls.last.arguments, {'appName': '[DEFAULT]', 'enabled': true});
    expect((await messaging.getNotificationSettings()).authorizationStatus,
        AuthorizationStatus.authorized);
    final permission =
        await messaging.requestPermission(alert: false, criticalAlert: true);
    expect(permission.sound, AppleNotificationSetting.notSupported);
    expect((calls.last.arguments as Map)['permissions']['criticalAlert'], true);
    final tokenEvent = messaging.onTokenRefresh.first;
    expect(
        await incoming('Messaging#onTokenRefresh', 'synthetic-refreshed-token'),
        null);
    expect(await tokenEvent, 'synthetic-refreshed-token');
    final foreground = FirebaseMessaging.onMessage.first;
    await incoming('Messaging#onMessage', {
      'messageId': 'foreground',
      'data': {'marker': 'one'}
    });
    expect((await foreground).data, {'marker': 'one'});
    final opened = FirebaseMessaging.onMessageOpenedApp.first;
    await incoming('Messaging#onMessageOpenedApp', {
      'messageId': 'warm',
      'data': {'marker': 'two'}
    });
    expect((await opened).messageId, 'warm');
    FirebaseMessaging.onBackgroundMessage(background);
    await Future<void>.delayed(Duration.zero);
    expect(rawHandle, isNotNull);
    expect(
        PluginUtilities.getCallbackFromHandle(
            CallbackHandle.fromRawHandle(rawHandle!)),
        background);
    failToken = true;
    await expectLater(
        messaging.getToken(),
        throwsA(isA<FirebaseException>()
            .having((e) => e.plugin, 'plugin', 'firebase_messaging')
            .having((e) => e.code, 'code', 'sdk-native-error')));
    expect(
        calls.any((c) =>
            c.method.contains('Installation') ||
            c.method.contains('refreshToken')),
        false);
  });
  test('background dispatcher waits for original user callback Future',
      () async {
    final backgroundHandle =
        PluginUtilities.getCallbackHandle(background)!.toRawHandle();
    backgroundMessages.clear();
    const target = 'plugins.flutter.io/firebase_messaging_background';
    binding.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel(target), (call) async {
      expect(call.method, 'MessagingBackground#initialized');
      return null;
    });
    backgroundGate = Completer<void>();
    flutterPhoneFirebaseMessagingBackgroundDispatcher();
    bool replied = false;
    final dispatch = incoming(
            'MessagingBackground#onMessage',
            {
              'userCallbackHandle': backgroundHandle,
              'message': {
                'messageId': 'background',
                'data': {'marker': 'three'}
              }
            },
            target: target)
        .then((value) {
      replied = true;
      return value;
    });
    await Future<void>.delayed(Duration.zero);
    expect(backgroundMessages, ['background']);
    expect(replied, false);
    backgroundGate!.complete();
    expect(await dispatch, null);
    expect(replied, true);
  });
}
