// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include "flutter/shell/platform/windows/uwp/platform_message_handler.h"

namespace flutter::winrt::plugins {
// Keep the dependency's generated Pigeon declarations in a separate translation
// unit: multiple published packages use the same messages.g.h include guard.
void RegisterFirebase(PlatformMessageHandlers& handlers);
HRESULT RegisterFirebaseApplicationServices(std::shared_ptr<void>* lifetime);
void HandleFirebaseNotificationActivation(
    ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs* args, bool initial);
}  // namespace flutter::winrt::plugins
