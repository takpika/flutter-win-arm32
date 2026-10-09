// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <firebase_compat/sdk.h>

#include "../firebase_core/configuration.h"

namespace flutter::winrt::plugins::firebase_messaging {
// The SDK owns Firebase settings, installations and transport state. Open a
// receiver first, restore Core's public setting, then publish network options.
// The serialized plugin worker supplies whether a Dart background observer is
// registered; delivery policy and persistence remain with the SDK.
// Keep this owner through asynchronous operation/context release. The process's
// normal import loader retains the Framework module used by these calls.
class SdkClient final {
 public:
  SdkClient(const firebase_core::ConfiguredApp& configured,
            const firebase_core::App& app, bool background_observer)
      : configured_(configured), core_app_id_(app.options.app_id()),
        background_observer_(background_observer) {
    if (!configured_.messaging ||
        app.options.project_id() != configured_.messaging->project_id)
      throw firebase_core::StateError("invalid-firebase-configuration");
    if (fcm_sdk_abi_version() < 21)
      throw firebase_core::StateError("sdk-version-unsupported");
    const auto descriptor = Descriptor();
    Check(fcm_sdk_open_with_core_identity(&descriptor, core_app_id_.c_str(),
                                          nullptr, &handle_));
  }
  ~SdkClient() {
    if (handle_) fcm_sdk_close(handle_);
  }
  SdkClient(const SdkClient&) = delete;
  SdkClient& operator=(const SdkClient&) = delete;

  fcm_sdk_handle handle() const { return handle_; }

  void RestoreDataCollection(firebase_core::App& app) {
    int enabled = 0;
    // Import is atomic and retryable. Even an old explicit value must not
    // overwrite a newer SDK value when initialization repeats after a crash.
    if (app.data_collection_source ==
        firebase_core::DataCollectionSource::kInherited) {
      Check(fcm_sdk_get_data_collection_default_enabled(handle_, &enabled));
    } else {
      Check(fcm_sdk_import_data_collection_default_enabled(
          handle_, app.automatic_data_collection ? 1 : 0, &enabled));
    }
    app.automatic_data_collection = enabled != 0;
    core_ready_ = true;
  }

  void SetDataCollection(bool enabled) {
    Check(fcm_sdk_set_data_collection_default_enabled(handle_, enabled ? 1 : 0));
    core_ready_ = true;
  }

  void PublishNetworkOptions() {
    if (!core_ready_)
      throw firebase_core::StateError("firebase-core-not-initialized");
    Reopen(background_observer_, true);
  }

  void SetBackgroundObserver(bool registered) {
    if (registered != background_observer_)
      Reopen(registered, network_published_);
  }

 private:
  void Reopen(bool observer, bool publish_network) {
    auto descriptor = Descriptor();
    descriptor.background_observer = observer ? 1 : 0;
    const auto& messaging = *configured_.messaging;
    const fcm_sdk_messaging_options options{
        sizeof(fcm_sdk_messaging_options), messaging.api_key.c_str(),
        messaging.sender_id.c_str(), messaging.android_package.c_str(),
        messaging.certificate_sha1.c_str()};
    fcm_sdk_handle initialized = 0;
    Check(fcm_sdk_open_with_core_identity(&descriptor, core_app_id_.c_str(),
                                          publish_network ? &options : nullptr,
                                          &initialized));
    const auto receiver = std::exchange(handle_, initialized);
    fcm_sdk_close(receiver);
    background_observer_ = observer;
    network_published_ = publish_network;
  }

  fcm_sdk_configuration Descriptor() const {
    const auto& messaging = *configured_.messaging;
    return {sizeof(fcm_sdk_configuration), configured_.name.c_str(),
            messaging.project_id.c_str(), messaging.app_id.c_str(), nullptr,
            background_observer_ ? 1 : 0};
  }
  static void Check(HRESULT result) {
    if (SUCCEEDED(result)) return;  // S_FALSE from atomic import is valid.
    const char* error = fcm_sdk_error_name(result);
    throw firebase_core::StateError(error ? error : "firebase-sdk-unavailable");
  }
  firebase_core::ConfiguredApp configured_;
  std::string core_app_id_;
  bool background_observer_;
  fcm_sdk_handle handle_ = 0;
  bool core_ready_ = false;
  bool network_published_ = false;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
