// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "notifications.h"

#include <firebase_compat/sdk.h>
#include "firebase_consumer_config.h"
#include "sdk_notification_settings.h"

#include "../firebase_core/state_document.h"

namespace flutter::winrt::plugins::firebase_messaging {
flutter::EncodableMap NativeSdkNotificationSettings(
    const flutter::EncodableMap* permissions) {
  using firebase_core::StateError;
  // Required Framework exports retain their original C ABI layouts.
  // The OS import loader resolves these required baseline exports and retains
  // the Framework module throughout the process, including this synchronous RPC.
  if (fcm_sdk_abi_version() < FLUTTER_FIREBASE_MINIMUM_ABI)
    throw StateError("sdk-version-unsupported");
  uint32_t requested = 0;
  if (permissions) {
    const std::pair<const char*, uint32_t> fields[] = {
        {"alert", FCM_SDK_NOTIFICATION_ALERT},
        {"announcement", FCM_SDK_NOTIFICATION_ANNOUNCEMENT},
        {"badge", FCM_SDK_NOTIFICATION_BADGE},
        {"carPlay", FCM_SDK_NOTIFICATION_CARPLAY},
        {"criticalAlert", FCM_SDK_NOTIFICATION_CRITICAL_ALERT},
        {"sound", FCM_SDK_NOTIFICATION_SOUND},
        {"provisional", FLUTTER_FIREBASE_PROVISIONAL_REQUEST},
        {"providesAppNotificationSettings", FCM_SDK_NOTIFICATION_APP_SETTINGS}};
    for (const auto& [name, bit] : fields) {
      const auto field = permissions->find(flutter::EncodableValue(name));
      if (field == permissions->end()) continue;
      const auto* enabled = std::get_if<bool>(&field->second);
      if (!enabled) throw StateError("invalid-argument");
      if (*enabled) requested |= bit;
    }
  }
  fcm_sdk_notification_settings settings{};
  settings.size = sizeof(settings);
  const HRESULT result = permissions
      ? fcm_sdk_request_notification_permission(requested, &settings)
      : fcm_sdk_get_notification_settings(&settings);
  if (FAILED(result)) {
    const char* code = fcm_sdk_error_name(result);
    throw StateError(code ? code : "notification-settings-unavailable");
  }
  return EncodeSdkNotificationSettings(settings);
}
}  // namespace flutter::winrt::plugins::firebase_messaging
