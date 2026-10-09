// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "../plugins/method_channel.h"

#include <cassert>
#include <iostream>

using namespace flutter;
class Messenger final : public BinaryMessenger {
 public:
  BinaryMessageHandler handler;
  void Send(const std::string&, const uint8_t*, size_t,
            BinaryReply) const override {
    assert(false);
  }
  void SetMessageHandler(const std::string& name,
                         BinaryMessageHandler value) override {
    assert(name == "original-channel");
    handler = std::move(value);
  }
};
int main() {
  Messenger messenger;
  std::unique_ptr<MethodResult<EncodableValue>> pending;
  flutter::winrt::plugins::RegisterMethodHandler(
      messenger, "original-channel", [&](const auto& call, auto result) {
        assert(call.method_name() == "original-method");
        assert(std::get<std::string>(*call.arguments()) == "日本語");
        pending = std::move(result);
      });
  const auto& codec = StandardMethodCodec::GetInstance();
  const auto call = codec.EncodeMethodCall(MethodCall<EncodableValue>(
      "original-method", std::make_unique<EncodableValue>("日本語")));
  int replies = 0;
  std::vector<uint8_t> answer;
  auto invoke = [&] {
    messenger.handler(call->data(), call->size(),
                      [&](const uint8_t* bytes, size_t size) {
                        ++replies;
                        answer.clear();
                        if (size) answer.assign(bytes, bytes + size);
                      });
    assert(pending);
  };
  invoke();
  assert(replies == 0);  // The reply survives the incoming stack frame.
  pending->Success(EncodableValue("保存済み"));
  assert(replies == 1);
  EncodableValue value("保存済み");
  assert(answer == *codec.EncodeSuccessEnvelope(&value));
  pending->Error("duplicate");
  assert(replies == 1);
  pending.reset();
  invoke();
  pending->Error("permission-denied", "synthetic", EncodableValue(false));
  value = EncodableValue(false);
  assert(replies == 2 &&
         answer == *codec.EncodeErrorEnvelope("permission-denied", "synthetic",
                                              &value));
  pending.reset();
  invoke();
  pending->NotImplemented();
  assert(replies == 3 && answer.empty());
  pending.reset();
  std::cout
      << "C embedder method channel: original codec, deferred results, errors, "
         "not-implemented and once-only reply PASS\n";
}
