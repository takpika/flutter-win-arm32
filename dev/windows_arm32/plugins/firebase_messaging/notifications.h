// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <flutter/encodable_value.h>
namespace flutter::winrt::plugins::firebase_messaging {
// Read-only shared SDK query; permission requests carry public Flutter options.
flutter::EncodableMap NativeSdkNotificationSettings(
    const flutter::EncodableMap* permissions);
}  // namespace flutter::winrt::plugins::firebase_messaging
