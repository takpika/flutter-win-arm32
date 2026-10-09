// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <string>

namespace flutter::winrt::plugins::firebase_messaging {
// Identity of the SDK Messaging client, independent of its Core app identity.
struct MessagingAppIdentity {
  std::string name, project_id, app_id;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
