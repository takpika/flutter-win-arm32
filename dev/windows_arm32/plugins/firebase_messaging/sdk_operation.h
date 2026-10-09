// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <firebase_compat/sdk.h>

#include "sdk_operation_reply.h"

namespace flutter::winrt::plugins::firebase_messaging {
namespace sdk_operation_detail {
inline fcm_sdk_operation_callbacks Callbacks(
    SdkOperationReply::Context* context) noexcept {
  fcm_sdk_operation_callbacks callbacks{};
  callbacks.size = sizeof(callbacks);
  callbacks.context = context;
  callbacks.completed = [](void* value, HRESULT result,
                           const fcm_sdk_buffer* token) noexcept {
    SdkOperationReply::Complete(
        static_cast<SdkOperationReply::Context*>(value),
        static_cast<SdkOperationReply::Status>(result),
        token ? token->data : nullptr, token ? token->size : 0);
  };
  callbacks.release = [](void* value) noexcept {
    SdkOperationReply::Release(
        static_cast<SdkOperationReply::Context*>(value));
  };
  return callbacks;
}
}  // namespace sdk_operation_detail

// The shared SDK performs the operation on its own WinRT worker. Keep its
// client owner until release; the reply uses WorkerMessenger's existing
// guarded channel reply, never a raw FlutterEngine or view pointer.
// The owner retains the loader reference through API return and release.
// The SDK itself pins its worker code for the process lifetime, including the
// worker epilogue that executes after the consumer's release callback.
// This call does not acquire an OS deferral. Background callers must retain
// their execution scope through the native operation's release as well.
inline HRESULT StartSdkOperation(fcm_sdk_handle client, int kind,
                                 SdkOperationReply::Reply reply,
                                 std::shared_ptr<void> owner,
                                 fcm_sdk_operation* operation) {
  static_assert(sizeof(HRESULT) == sizeof(SdkOperationReply::Status));
  return static_cast<HRESULT>(SdkOperationReply::Start(
      [client, kind, operation](SdkOperationReply::Context* context) noexcept {
        auto callbacks = sdk_operation_detail::Callbacks(context);
        return static_cast<SdkOperationReply::Status>(
            fcm_sdk_start_operation(client, kind, &callbacks, operation));
      },
      std::move(reply), std::move(owner)));
}
}  // namespace flutter::winrt::plugins::firebase_messaging
