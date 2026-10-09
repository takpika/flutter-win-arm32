// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <cassert>
#include <iostream>
#include <vector>

#include "flutter/shell/platform/windows/uwp/binary_messenger.h"

struct _FlutterPlatformMessageResponseHandle {
  FlutterDataCallback callback;
  void* data;
};

struct FakeEngine {
  FlutterEngineProcTable api{};
  FlutterPlatformMessageResponseHandle incoming{};
  FlutterDataCallback callback = nullptr;
  void* baton = nullptr;
  bool fail_create = false, fail_send = false, immediate = false;
  int creates = 0, releases = 0, sends = 0, responses = 0;
  std::string channel;
  std::vector<uint8_t> payload;

  FakeEngine() {
    api.struct_size = sizeof(api);
    api.PlatformMessageCreateResponseHandle = Create;
    api.PlatformMessageReleaseResponseHandle = Release;
    api.SendPlatformMessage = Send;
    api.SendPlatformMessageResponse = Respond;
  }
  FlutterEngine handle() { return reinterpret_cast<FlutterEngine>(this); }
  static FakeEngine& Get(FlutterEngine engine) {
    return *reinterpret_cast<FakeEngine*>(engine);
  }
  static FlutterEngineResult Create(
      FlutterEngine engine, FlutterDataCallback callback, void* baton,
      FlutterPlatformMessageResponseHandle** result) {
    auto& fake = Get(engine);
    ++fake.creates;
    if (fake.fail_create) return kInternalInconsistency;
    *result = new FlutterPlatformMessageResponseHandle{callback, baton};
    return kSuccess;
  }
  static FlutterEngineResult Release(
      FlutterEngine engine, FlutterPlatformMessageResponseHandle* handle) {
    ++Get(engine).releases;
    delete handle;
    return kSuccess;
  }
  static FlutterEngineResult Send(FlutterEngine engine,
                                  const FlutterPlatformMessage* message) {
    auto& fake = Get(engine);
    ++fake.sends;
    fake.channel = message->channel;
    fake.payload.clear();
    if (message->message_size)
      fake.payload.assign(message->message,
                          message->message + message->message_size);
    if (fake.fail_send) return kInternalInconsistency;
    if (const auto* response = message->response_handle) {
      fake.callback = response->callback;
      fake.baton = response->data;
      if (fake.immediate) fake.Deliver({8, 9});
    }
    return kSuccess;
  }
  static FlutterEngineResult Respond(
      FlutterEngine engine, const FlutterPlatformMessageResponseHandle* handle,
      const uint8_t* data, size_t size) {
    auto& fake = Get(engine);
    assert(handle == &fake.incoming);
    ++fake.responses;
    fake.payload.clear();
    if (size) fake.payload.assign(data, data + size);
    return kSuccess;
  }
  void Deliver(const std::vector<uint8_t>& bytes) {
    auto reply = callback;
    auto* context = baton;
    callback = nullptr;
    baton = nullptr;
    assert(reply);
    reply(bytes.data(), bytes.size(), context);
  }
  void Shutdown() {
    callback = nullptr;
    baton = nullptr;
  }
};

int main() {
  FakeEngine engine;
  flutter::winrt::EmbedderMessenger messenger;
  messenger.Attach(engine.handle(), engine.api);
  const uint8_t bytes[] = {1, 0, 255};
  int replies = 0;
  messenger.Send("outgoing", bytes, sizeof(bytes),
                 [&](const uint8_t* data, size_t size) {
                   ++replies;
                   assert(size == 2 && data[0] == 4 && data[1] == 5);
                 });
  assert(engine.channel == "outgoing");
  assert(engine.payload == std::vector<uint8_t>({1, 0, 255}));
  assert(engine.creates == 1 && engine.releases == 1 && replies == 0);
  engine.Deliver({4, 5});
  assert(replies == 1);

  // A reply can arrive within Send; its baton must not be touched afterward.
  engine.immediate = true;
  messenger.Send("synchronous", nullptr, 0,
                 [&](const uint8_t* data, size_t size) {
                   ++replies;
                   assert(size == 2 && data[0] == 8);
                 });
  engine.immediate = false;
  assert(replies == 2 && engine.releases == 2);

  auto unavailable = [&](const uint8_t* data, size_t size) {
    ++replies;
    assert(!data && !size);
  };
  engine.fail_create = true;
  messenger.Send("create-error", nullptr, 0, unavailable);
  engine.fail_create = false;
  engine.fail_send = true;
  messenger.Send("send-error", nullptr, 0, unavailable);
  engine.fail_send = false;
  assert(replies == 4 && engine.creates == 4 && engine.releases == 3);

  flutter::BinaryReply delayed;
  messenger.SetMessageHandler("incoming", [&](const uint8_t* data, size_t size,
                                              flutter::BinaryReply reply) {
    assert(size == 3 && data[2] == 255);
    messenger.SetMessageHandler("incoming", nullptr);
    delayed = std::move(reply);
  });
  FlutterPlatformMessage message{};
  message.struct_size = sizeof(message);
  message.channel = "incoming";
  message.message = bytes;
  message.message_size = sizeof(bytes);
  message.response_handle = &engine.incoming;
  assert(messenger.Handle(&message));
  assert(!messenger.Handle(&message));
  delayed(bytes, sizeof(bytes));
  delayed(bytes, sizeof(bytes));
  assert(engine.responses == 1);

  // Clearing plugins must not free batons still held by the live engine.
  messenger.Send("during-shutdown", nullptr, 0, unavailable);
  messenger.Stop();
  engine.Deliver({6});
  assert(replies == 5);
  messenger.Send("stopped", nullptr, 0, unavailable);
  assert(replies == 6);
  engine.Shutdown();
  messenger.EngineDidShutdown();
  delayed(bytes, sizeof(bytes));
  assert(engine.responses == 1);

  // A pending response with no Dart reply is reclaimed only after shutdown.
  messenger.Attach(engine.handle(), engine.api);
  messenger.Send("abandoned", nullptr, 0, unavailable);
  messenger.Stop();
  engine.Shutdown();
  messenger.EngineDidShutdown();
  assert(replies == 7);

  // Asynchronous incoming replies may outlive both shutdown and the messenger.
  flutter::BinaryReply after_destruction;
  {
    flutter::winrt::EmbedderMessenger other;
    other.Attach(engine.handle(), engine.api);
    other.SetMessageHandler(
        "incoming", [&](const uint8_t*, size_t, flutter::BinaryReply reply) {
          after_destruction = std::move(reply);
        });
    assert(other.Handle(&message));
    other.Stop();
    engine.Shutdown();
    other.EngineDidShutdown();
  }
  after_destruction(bytes, sizeof(bytes));
  assert(engine.responses == 1);
  std::cout << "Embedder messenger: channels, async/inline replies, failures, "
               "shutdown and late callbacks PASS\n";
}
