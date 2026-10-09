// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <firebase_compat/sdk.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

#include "initial_message_cache.h"

namespace flutter::winrt::plugins::firebase_messaging {
// Called on the SDK's worker, under its callback's real execution scope.
// The emitter uses the existing weak messenger lifetime guard. No engine,
// borrowed JSON or SDK cancellation token escapes into its eventual reply.
class SdkMessageHandler {
 public:
  using Completion = std::function<void(bool)>;
  using Emit =
      std::function<bool(const std::string&, EncodableValue, Completion)>;
  using Canceled = std::function<bool()>;
  using Background =
      std::function<HRESULT(const EncodableMap&, const Canceled&)>;
  using Opened =
      std::function<bool(fcm_sdk_handle, int64_t, const uint8_t*, size_t)>;

  SdkMessageHandler(MessagingAppIdentity app, InitialMessageCache cache,
                    Emit emit, Background background, Opened opened = {})
      : app_(std::move(app)),
        cache_(std::move(cache)),
        emit_(std::move(emit)),
        background_(std::move(background)),
        opened_(std::move(opened)) {}

  HRESULT Dispatch(int kind, int64_t receipt, const uint8_t* json, size_t size,
                   const Canceled& canceled,
                   fcm_sdk_handle client = 0) const noexcept {
    if (!canceled || receipt <= 0) return E_INVALIDARG;
    try {
      if (canceled()) return E_ABORT;
      auto message = DecodeSdkRemoteMessage(json, size);
      // MESSAGE also includes background data delivery. The SDK observes every
      // real OS view; the Flutter callback must follow that state, not the
      // delivery kind or the existence of an engine/isolate.
      int foreground = 0;
      if (kind == FCM_SDK_MESSAGE) {
        const HRESULT result = fcm_sdk_get_foreground_state(&foreground);
        if (result != S_OK) return result;
        if (canceled()) return E_ABORT;
      }
      if (kind == FCM_SDK_BACKGROUND_OBSERVER ||
          (kind == FCM_SDK_MESSAGE && !foreground))
        return background_ ? background_(message, canceled)
                           : HRESULT_FROM_WIN32(ERROR_NOT_READY);
      if (kind == FCM_SDK_NOTIFICATION_OPENED &&
          (cache_.Contains(app_, receipt) ||
           (opened_ && opened_(client, receipt, json, size))))
        return S_OK;  // Full cold message already has durable Flutter custody.
      const char* method = kind == FCM_SDK_MESSAGE ? "Messaging#onMessage"
                           : kind == FCM_SDK_NOTIFICATION_OPENED
                               ? "Messaging#onMessageOpenedApp"
                               : nullptr;
      if (!method) return E_INVALIDARG;
      return Send(method, std::move(message), canceled);
    } catch (...) {
      return E_FAIL;  // Never throw through the SDK C callback boundary.
    }
  }

 private:
  struct Reply {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false, success = false;
  };
  HRESULT Send(const char* method, EncodableMap message,
               const Canceled& canceled) const {
    if (!emit_) return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    auto reply = std::make_shared<Reply>();
    if (!emit_(method, EncodableValue(std::move(message)),
               [reply](bool success) {
                 std::lock_guard<std::mutex> lock(reply->mutex);
                 if (reply->done) return;
                 reply->done = true;
                 reply->success = success;
                 reply->changed.notify_one();
               }))
      return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    std::unique_lock<std::mutex> lock(reply->mutex);
    while (!reply->done) {
      if (canceled()) return E_ABORT;
      reply->changed.wait_for(lock, std::chrono::milliseconds(100));
    }
    return reply->success ? S_OK : E_FAIL;
  }
  MessagingAppIdentity app_;
  InitialMessageCache cache_;
  Emit emit_;
  Background background_;
  Opened opened_;
};
}  // namespace flutter::winrt::plugins::firebase_messaging
