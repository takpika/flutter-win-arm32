// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <firebase_compat/notification_settings.h>
#include <flutter/encodable_value.h>

namespace flutter::winrt::plugins::firebase_messaging {
// FlutterFire's wire integers differ from both the SDK and Dart enum indices.
// OS policy and supported fields are determined exclusively by the shared SDK.
inline EncodableMap EncodeSdkNotificationSettings(
    const fcm_sdk_notification_settings& settings) {
  int authorization = -1;
  if (settings.supported_fields & FCM_SDK_NOTIFICATION_AUTHORIZATION) {
    switch (settings.authorization) {
      case FCM_SDK_NOTIFICATION_AUTHORIZED:
        authorization = 1;
        break;
      case FCM_SDK_NOTIFICATION_DENIED:
        authorization = 0;
        break;
    }
  }
  // Omitted Apple-only fields decode to notSupported in FlutterFire. The SDK
  // does not expose individual Apple settings, so do not infer them from auth.
  return {{EncodableValue("authorizationStatus"), EncodableValue(authorization)}};
}
}  // namespace flutter::winrt::plugins::firebase_messaging
