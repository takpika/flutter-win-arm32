// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_core_platform_interface/firebase_core_platform_interface.dart' as core;
import 'package:firebase_messaging_platform_interface/firebase_messaging_platform_interface.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';


// Registered only by the SDK's Windows Phone build target. Neither the app's
// sources nor Flutter's Windows platform identity are changed.
class WindowsPhoneFirebaseMessaging extends FirebaseMessagingPlatform {
  WindowsPhoneFirebaseMessaging._([FirebaseApp? app]) : super(appInstance: app);

  static const MethodChannel _channel = MethodChannel('plugins.flutter.io/firebase_messaging');
  static const String backgroundEntrypoint = 'flutterPhoneFirebaseMessagingBackgroundDispatcher';
  static final StreamController<String> _tokens = StreamController<String>.broadcast();
  static String? _dispatcherLibraryUri;
  static Future<void> _ready = Future<void>.value();
  static bool _channelsInitialized = false;
  static bool _licenseRegistered = false;
  static Object? _startupError;
  static StackTrace? _startupStack;
  bool _autoInitEnabled = false;

  static void registerWith({required String dispatcherLibraryUri}) {
    _dispatcherLibraryUri = dispatcherLibraryUri;
    _startupError = null;
    _startupStack = null;
    _channelsInitialized = false;
    _ready = Future<void>.value();
    FirebaseMessagingPlatform.instance = WindowsPhoneFirebaseMessaging._();
    final core.FirebasePlatform upstream = core.FirebasePlatform.instance;
    if (upstream is! _MessagingCoreBootstrap) {
      core.FirebasePlatform.instance = _MessagingCoreBootstrap(upstream);
    }
    if (!_licenseRegistered) {
      _licenseRegistered = true;
      LicenseRegistry.addLicense(() async* {
        final String text = await rootBundle.loadString(
          'packages/flutter_windows_arm32/firebase_messaging/NOTICE.txt',
        );
        yield LicenseEntryWithLineBreaks(<String>['wpnotify', 'firebase-compat'], text);
      });
    }
  }

  // The generated registrant runs before app main, so it must not choose the
  // app's binding or zone. Core initialization and public Messaging calls
  // reach this only once the app has initialized its own services binding.
  static void _ensureChannels() {
    if (_channelsInitialized) return;
    _channel.setMethodCallHandler(_handleMessage);
    _channelsInitialized = true;
    _ready = _channel
        .invokeMethod<void>('Messaging#initialized', <String, Object?>{
          'dispatcherLibraryUri': _dispatcherLibraryUri,
          'dispatcherEntrypoint': backgroundEntrypoint,
        })
        .then<void>(
          (_) {},
          onError: (Object error, StackTrace stack) {
            _startupError = error;
            _startupStack = stack;
          },
        );
  }

  static Future<dynamic> _handleMessage(MethodCall call) async {
    switch (call.method) {
      case 'Messaging#onTokenRefresh':
        if (call.arguments is! String) throw _invalidResponse();
        _tokens.add(call.arguments as String);
        return null;
      case 'Messaging#onMessage':
        FirebaseMessagingPlatform.onMessage.add(_message(call.arguments));
        return null;
      case 'Messaging#onMessageOpenedApp':
        FirebaseMessagingPlatform.onMessageOpenedApp.add(_message(call.arguments));
        return null;
      case 'Messaging#onBackgroundMessage':
        final BackgroundMessageHandler? handler = FirebaseMessagingPlatform.onBackgroundMessage;
        if (handler == null) throw _error('missing-background-handler');
        await handler(_message(call.arguments));
        return null;
      default:
        throw _error('unknown-native-event');
    }
  }

  static FirebaseException _error(String code) =>
      FirebaseException(plugin: 'firebase_messaging', code: code);
  static FirebaseException _invalidResponse() => _error('invalid-response');

  static Map<String, dynamic> _map(dynamic value) {
    if (value is! Map || value.keys.any((dynamic key) => key is! String)) {
      throw _invalidResponse();
    }
    return Map<String, dynamic>.from(value);
  }

  static RemoteMessage _message(dynamic value) => RemoteMessage.fromMap(_map(value));

  static Future<dynamic> _call(String method, Map<String, Object?> arguments) async {
    try {
      await _awaitReady();
      return await _channel.invokeMethod<dynamic>(method, arguments);
    } on PlatformException catch (error, stack) {
      Error.throwWithStackTrace(
        FirebaseException(plugin: 'firebase_messaging', code: error.code, message: error.message),
        stack,
      );
    }
  }

  static Future<void> _awaitReady() async {
    _ensureChannels();
    await _ready;
    final Object? error = _startupError;
    if (error is PlatformException) {
      Error.throwWithStackTrace(
        FirebaseException(plugin: 'firebase_messaging', code: error.code, message: error.message),
        _startupStack!,
      );
    }
    if (error != null) Error.throwWithStackTrace(error, _startupStack!);
  }

  static Future<String?> _tokenFor(String name) async {
    final Map<String, dynamic> response = _map(
      await _call('Messaging#getToken', <String, Object?>{'appName': name}),
    );
    final Object? token = response['token'];
    if (token != null && token is! String) throw _invalidResponse();
    return token as String?;
  }

  @override
  FirebaseMessagingPlatform delegateFor({required FirebaseApp app}) =>
      WindowsPhoneFirebaseMessaging._(app);

  @override
  FirebaseMessagingPlatform setInitialValues({bool? isAutoInitEnabled}) {
    _autoInitEnabled = isAutoInitEnabled ?? false;
    return this;
  }

  @override
  bool get isAutoInitEnabled => _autoInitEnabled;

  @override
  Future<bool> isSupported() async {
    final dynamic result = await _call('Messaging#isSupported', <String, Object?>{});
    if (result is! bool) throw _invalidResponse();
    return result;
  }

  @override
  Future<RemoteMessage?> getInitialMessage() async {
    final dynamic value = await _call('Messaging#getInitialMessage', <String, Object?>{
      'appName': app.name,
    });
    return value == null ? null : _message(value);
  }

  @override
  Future<void> registerBackgroundMessageHandler(BackgroundMessageHandler handler) async {
    final CallbackHandle? handle = PluginUtilities.getCallbackHandle(handler);
    if (handle == null || _dispatcherLibraryUri == null) throw _error('invalid-background-handler');
    await _call('Messaging#startBackgroundIsolate', <String, Object?>{
      'userCallbackHandle': handle.toRawHandle(),
      'dispatcherLibraryUri': _dispatcherLibraryUri,
      'dispatcherEntrypoint': backgroundEntrypoint,
    });
  }

  @override
  Future<void> deleteToken() async {
    // Deleting an FCM token does not delete the Firebase Installation shared
    // with the app's Firebase services.
    await _call('Messaging#deleteToken', <String, Object?>{'appName': app.name});
  }

  @override
  Future<String?> getAPNSToken() async => null; // APNs is an Apple-only API.

  @override
  Future<String?> getToken({String? vapidKey, String? serviceWorkerScriptPath}) =>
      _tokenFor(app.name);

  @override
  Stream<String> get onTokenRefresh => _tokens.stream;

  static NotificationSettings _settings(dynamic value) {
    final Map<String, dynamic> map = _map(value);
    final AuthorizationStatus authorization = switch (map['authorizationStatus']) {
      -1 => AuthorizationStatus.notDetermined,
      0 => AuthorizationStatus.denied,
      1 => AuthorizationStatus.authorized,
      2 => AuthorizationStatus.provisional,
      3 => AuthorizationStatus.deniedPermanently,
      _ => throw _invalidResponse(),
    };
    AppleNotificationSetting setting(String key) => switch (map[key]) {
      0 => AppleNotificationSetting.disabled,
      1 => AppleNotificationSetting.enabled,
      _ => AppleNotificationSetting.notSupported,
    };
    return NotificationSettings(
      authorizationStatus: authorization,
      alert: setting('alert'),
      announcement: setting('announcement'),
      badge: setting('badge'),
      carPlay: setting('carPlay'),
      lockScreen: setting('lockScreen'),
      notificationCenter: setting('notificationCenter'),
      sound: setting('sound'),
      timeSensitive: setting('timeSensitive'),
      criticalAlert: setting('criticalAlert'),
      providesAppNotificationSettings: setting('providesAppNotificationSettings'),
      showPreviews: switch (map['showPreviews']) {
        0 => AppleShowPreviewSetting.never,
        1 => AppleShowPreviewSetting.always,
        2 => AppleShowPreviewSetting.whenAuthenticated,
        _ => AppleShowPreviewSetting.notSupported,
      },
    );
  }

  @override
  Future<NotificationSettings> getNotificationSettings() async => _settings(
    await _call('Messaging#getNotificationSettings', <String, Object?>{'appName': app.name}),
  );

  @override
  Future<NotificationSettings> requestPermission({
    bool alert = true,
    bool announcement = false,
    bool badge = true,
    bool carPlay = false,
    bool criticalAlert = false,
    bool provisional = false,
    bool sound = true,
    bool providesAppNotificationSettings = false,
  }) async => _settings(
    await _call('Messaging#requestPermission', <String, Object?>{
      'appName': app.name,
      'permissions': <String, bool>{
        'alert': alert,
        'announcement': announcement,
        'badge': badge,
        'carPlay': carPlay,
        'criticalAlert': criticalAlert,
        'provisional': provisional,
        'sound': sound,
        'providesAppNotificationSettings': providesAppNotificationSettings,
      },
    }),
  );

  @override
  Future<void> setAutoInitEnabled(bool enabled) async {
    final Map<String, dynamic> response = _map(
      await _call('Messaging#setAutoInitEnabled', <String, Object?>{
        'appName': app.name,
        'enabled': enabled,
      }),
    );
    final Object? actual = response['isAutoInitEnabled'];
    if (actual is! bool) throw _invalidResponse();
    _autoInitEnabled = actual;
  }

  @override
  Future<void> setForegroundNotificationPresentationOptions({
    required bool alert,
    required bool badge,
    required bool sound,
  }) async {
    // Match the published Windows/Android contract: this API configures Apple
    // presentation only. It must not initialize Messaging or persist policy.
  }

  @override
  Future<void> subscribeToTopic(String topic) async {
    await _call('Messaging#subscribeToTopic', <String, Object?>{
      'appName': app.name,
      'topic': topic,
    });
  }

  @override
  Future<void> unsubscribeFromTopic(String topic) async {
    await _call('Messaging#unsubscribeFromTopic', <String, Object?>{
      'appName': app.name,
      'topic': topic,
    });
  }

  @override
  Future<void> setDeliveryMetricsExportToBigQuery(bool enabled) async {
    await _call('Messaging#setDeliveryMetricsExportToBigQuery', <String, Object?>{
      'appName': app.name,
      'enabled': enabled,
    });
  }
}

// Delegate the original public Core implementation and its Pigeon protocol.
// This hook starts the Phone channel after the app's binding exists, including
// apps which rely on default auto-init without first requesting a token.
class _MessagingCoreBootstrap extends core.FirebasePlatform {
  _MessagingCoreBootstrap(this.upstream);
  final core.FirebasePlatform upstream;

  @override
  List<core.FirebaseAppPlatform> get apps => upstream.apps;

  @override
  core.FirebaseAppPlatform app([String name = core.defaultFirebaseAppName]) => upstream.app(name);

  @override
  Future<core.FirebaseAppPlatform> initializeApp({String? name, FirebaseOptions? options}) async {
    if (FirebaseMessagingPlatform.instance is WindowsPhoneFirebaseMessaging) {
      await WindowsPhoneFirebaseMessaging._awaitReady();
    }
    return upstream.initializeApp(name: name, options: options);
  }
}

@pragma('vm:entry-point')
void flutterPhoneFirebaseMessagingBackgroundDispatcher() {
  WidgetsFlutterBinding.ensureInitialized();
  const MethodChannel channel = MethodChannel('plugins.flutter.io/firebase_messaging_background');
  channel.setMethodCallHandler((MethodCall call) async {
    if (call.method != 'MessagingBackground#onMessage') {
      throw WindowsPhoneFirebaseMessaging._error('unknown-background-event');
    }
    final Map<String, dynamic> arguments = WindowsPhoneFirebaseMessaging._map(call.arguments);
    final Object? rawHandle = arguments['userCallbackHandle'];
    if (rawHandle is! int) throw WindowsPhoneFirebaseMessaging._invalidResponse();
    final Function? callback = PluginUtilities.getCallbackFromHandle(
      CallbackHandle.fromRawHandle(rawHandle),
    );
    if (callback is! BackgroundMessageHandler) {
      throw WindowsPhoneFirebaseMessaging._error('missing-background-handler');
    }
    await callback(WindowsPhoneFirebaseMessaging._message(arguments['message']));
    return null;
  });
  unawaited(channel.invokeMethod<void>('MessagingBackground#initialized'));
}
