// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../sdk_notification_settings.h"
#include <flutter/standard_message_codec.h>
#include <cassert>
#include <iostream>

using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::winrt::plugins::firebase_messaging::EncodeSdkNotificationSettings;

// This is the public MethodChannel wire contract, not AuthorizationStatus.index.
// The Dart decoder maps 1 to authorized, 0 to denied, -1 to notDetermined.
void Check(int native, int sdk, unsigned supported, int wire) {
  fcm_sdk_notification_settings settings{};
  settings.size = sizeof(settings);
  settings.windows_setting = native;
  settings.authorization = sdk;
  settings.supported_fields = supported;
  const auto map = EncodeSdkNotificationSettings(settings);
  assert(map.size() == 1);  // Unsupported Apple fields must stay omitted.
  assert(std::get<int32_t>(map.at(EncodableValue("authorizationStatus"))) == wire);
  auto& codec = flutter::StandardMessageCodec::GetInstance();
  const auto bytes = codec.EncodeMessage(EncodableValue(map));
  assert(*codec.DecodeMessage(*bytes) == EncodableValue(map));
  std::cout << std::get<int32_t>(map.at(EncodableValue("authorizationStatus"))) << "\n";
}

int main() {
  Check(0, FCM_SDK_NOTIFICATION_AUTHORIZED, FCM_SDK_NOTIFICATION_AUTHORIZATION, 1);
  for (int native = 1; native <= 4; ++native)
    Check(native, FCM_SDK_NOTIFICATION_DENIED, FCM_SDK_NOTIFICATION_AUTHORIZATION, 0);
  // A future native value must not become denied or authorized by guessing.
  Check(99, FCM_SDK_NOTIFICATION_UNSUPPORTED, 0, -1);
  Check(99, 99, FCM_SDK_NOTIFICATION_AUTHORIZATION, -1);
  Check(0, FCM_SDK_NOTIFICATION_AUTHORIZED, 0, -1);
}
