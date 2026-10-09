// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "sdk_operation.h"
#include <string>

namespace flutter::winrt::plugins::firebase_messaging {
// The SDK owns durable intent, normalization, retry and terminal completion.
// Forward its Future to Flutter using the same guarded RPC lifetime as tokens.
inline HRESULT StartSdkTopicOperation(
    fcm_sdk_handle client, std::string topic, bool subscribe,
    SdkOperationReply::Reply reply, std::shared_ptr<void> owner,
    fcm_sdk_operation* operation) {
  return static_cast<HRESULT>(SdkOperationReply::Start(
      [client, topic = std::move(topic), subscribe, operation](
          SdkOperationReply::Context* context) noexcept {
        auto callbacks = sdk_operation_detail::Callbacks(context);
        return static_cast<SdkOperationReply::Status>(
            fcm_sdk_start_topic_operation(client, topic.c_str(), subscribe ? 1 : 0,
                                          &callbacks, operation));
      },
      std::move(reply), std::move(owner)));
}
}  // namespace flutter::winrt::plugins::firebase_messaging
