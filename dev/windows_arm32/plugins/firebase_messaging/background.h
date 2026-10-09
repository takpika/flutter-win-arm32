// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <functional>
#include <flutter/encodable_value.h>
#include "flutter/shell/platform/windows/uwp/runtime_support.h"

namespace flutter::winrt::plugins::firebase_messaging {
// Consumes an owned, SDK-normalized public message. Does not accept/ACK a
// transport message, mutate SDK work stages, display a toast or retry delivery.
// The SDK caller retains its OS execution scope until this synchronous callback
// returns and interprets failure as unfinished Dart work.
HRESULT DeliverSdkBackground(const EncodableMap& message,
                             const std::function<bool()>& canceled);
}  // namespace flutter::winrt::plugins::firebase_messaging
