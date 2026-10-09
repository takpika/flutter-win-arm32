// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <flutter/binary_messenger.h>

#include <cassert>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter::winrt {

// Platform-thread BinaryMessenger for the C embedder. The engine owner calls
// Stop before disposing plugins, then EngineDidShutdown after engine shutdown.
// Pending callback batons must outlive shutdown: the engine can still own them
// after their public response handles have been released.
class EmbedderMessenger final : public BinaryMessenger {
 public:
  EmbedderMessenger() : state_(std::make_shared<State>()) {}
  ~EmbedderMessenger() override { assert(state_->pending.empty()); }
  EmbedderMessenger(const EmbedderMessenger&) = delete;
  EmbedderMessenger& operator=(const EmbedderMessenger&) = delete;

  void Attach(FlutterEngine engine, const FlutterEngineProcTable& api) {
    if (state_->engine || !engine || !api.SendPlatformMessage ||
        !api.SendPlatformMessageResponse ||
        !api.PlatformMessageCreateResponseHandle ||
        !api.PlatformMessageReleaseResponseHandle) {
      throw std::invalid_argument("Invalid embedder messenger attachment");
    }
    state_->engine = engine;
    state_->api = api;
    state_->active = true;
  }

  void Stop() { state_->active = false; }

  void EngineDidShutdown() {
    Stop();
    state_->engine = nullptr;
    handlers_.clear();
    // No engine callback can access these batons after shutdown. Resolve
    // abandoned requests once, after removing them to permit reentrancy.
    auto pending = std::move(state_->pending);
    state_->pending.clear();
    for (auto& entry : pending) entry.second->reply(nullptr, 0);
  }

  void Send(const std::string& channel, const uint8_t* data, size_t size,
            BinaryReply reply = nullptr) const override {
    auto state = state_;
    if (!state->active || channel.find('\0') != std::string::npos ||
        (!data && size)) {
      if (reply) reply(nullptr, 0);
      return;
    }
    FlutterPlatformMessage message{};
    message.struct_size = sizeof(message);
    message.channel = channel.c_str();
    message.message = data;
    message.message_size = size;
    if (!reply) {
      state->api.SendPlatformMessage(state->engine, &message);
      return;
    }
    const size_t id = ++state->next_id;
    auto baton = std::make_unique<Pending>();
    baton->owner = state.get();
    baton->id = id;
    baton->reply = std::move(reply);
    auto* raw = baton.get();
    state->pending.emplace(id, std::move(baton));
    FlutterPlatformMessageResponseHandle* handle = nullptr;
    auto result = state->api.PlatformMessageCreateResponseHandle(
        state->engine, OnReply, raw, &handle);
    if (result == kSuccess) {
      message.response_handle = handle;
      result = state->api.SendPlatformMessage(state->engine, &message);
      state->api.PlatformMessageReleaseResponseHandle(state->engine, handle);
    }
    if (result != kSuccess) {
      const auto found = state->pending.find(id);
      if (found != state->pending.end()) {
        auto failed = std::move(found->second);
        state->pending.erase(found);
        failed->reply(nullptr, 0);
      }
    }
  }

  void SetMessageHandler(const std::string& channel,
                         BinaryMessageHandler handler) override {
    if (handler)
      handlers_.insert_or_assign(channel, std::move(handler));
    else
      handlers_.erase(channel);
  }

  bool Handle(const FlutterPlatformMessage* message) {
    if (!state_->active || !message || !message->channel) return false;
    const auto found = handlers_.find(message->channel);
    if (found == handlers_.end()) return false;
    // The callback can replace/unregister its own channel.
    auto handler = found->second;
    auto answered = std::make_shared<bool>(false);
    std::weak_ptr<State> weak = state_;
    const auto* response = message->response_handle;
    handler(message->message, message->message_size,
            [weak, answered, response](const uint8_t* data, size_t size) {
              if (std::exchange(*answered, true)) return;
              auto state = weak.lock();
              if (state && state->active && response)
                state->api.SendPlatformMessageResponse(state->engine, response,
                                                       data, size);
            });
    return true;
  }

 private:
  struct State;
  struct Pending {
    State* owner;
    size_t id;
    BinaryReply reply;
  };
  struct State {
    FlutterEngine engine = nullptr;
    FlutterEngineProcTable api{};
    bool active = false;
    size_t next_id = 0;
    std::map<size_t, std::unique_ptr<Pending>> pending;
  };
  static void OnReply(const uint8_t* data, size_t size, void* user_data) {
    auto* raw = static_cast<Pending*>(user_data);
    auto* state = raw->owner;
    const auto found = state->pending.find(raw->id);
    auto pending = std::move(found->second);
    state->pending.erase(found);
    if (state->active)
      pending->reply(data, size);
    else
      pending->reply(nullptr, 0);
  }

  std::shared_ptr<State> state_;
  std::map<std::string, BinaryMessageHandler> handlers_;
};
}  // namespace flutter::winrt
