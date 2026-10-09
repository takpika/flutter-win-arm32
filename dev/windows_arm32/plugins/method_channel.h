// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <flutter/method_channel.h>
#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>

namespace flutter::winrt::plugins {
// C-embedder binding for the public codec/result interfaces. The desktop
// wrapper's EngineMethodResult is coupled to its FlutterDesktopMessenger C API.
// This host uses BinaryMessenger directly, including its weak engine lifetime.
inline void RegisterMethodHandler(BinaryMessenger& messenger,
                                  const std::string& name,
                                  MethodCallHandler<EncodableValue> handler) {
  messenger.SetMessageHandler(
      name, [handler = std::move(handler)](const uint8_t* data, size_t size,
                                           BinaryReply reply) {
        const auto& codec = StandardMethodCodec::GetInstance();
        auto answered = std::make_shared<bool>(false);
        auto respond = [reply,
                        answered](std::unique_ptr<std::vector<uint8_t>> bytes) {
          if (std::exchange(*answered, true)) return;
          reply(bytes && !bytes->empty() ? bytes->data() : nullptr,
                bytes ? bytes->size() : 0);
        };
        auto call = codec.DecodeMethodCall(data, size);
        if (!call) {
          respond(codec.EncodeErrorEnvelope("invalid-method-call",
                                            "Unable to decode method call"));
          return;
        }
        auto result = std::make_unique<MethodResultFunctions<EncodableValue>>(
            [respond](const EncodableValue* value) {
              respond(StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(
                  value));
            },
            [respond](const std::string& code, const std::string& message,
                      const EncodableValue* details) {
              respond(StandardMethodCodec::GetInstance().EncodeErrorEnvelope(
                  code, message, details));
            },
            [respond] { respond(nullptr); });
        handler(*call, std::move(result));
      });
}
}  // namespace flutter::winrt::plugins
