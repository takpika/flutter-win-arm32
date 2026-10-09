// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace flutter::winrt::plugins::firebase_messaging {
// Retains native RPC state, not a Flutter engine. The reply must use the
// messenger's existing platform-thread/lifetime guard. This is not a delivery
// acknowledgement: the shared SDK still owns inbox stages and replay.
class SdkOperationReply {
 public:
  using Status = int32_t;
  using Bytes = std::vector<uint8_t>;
  using Reply = std::function<void(Status, Bytes)>;

 private:
  struct State {
    Reply reply;
    std::shared_ptr<void> owner;
    std::atomic<bool> completed{false};
    State(Reply reply, std::shared_ptr<void> owner)
        : reply(std::move(reply)), owner(std::move(owner)) {}
  };

 public:
  struct Context {
    std::shared_ptr<State> state;
  };

  // Exactly S_OK transfers the context reference to the SDK. In particular,
  // completion AND release may already have run before begin returns. Keep a
  // separate caller reference so its module cannot unload during that call.
  template <typename Begin>
  static Status Start(Begin begin, Reply reply, std::shared_ptr<void> owner) {
    static_assert(std::is_nothrow_invocable_r_v<Status, Begin, Context*>);
    if (!reply) throw std::invalid_argument("Missing SDK operation reply");
    auto caller = std::make_shared<State>(std::move(reply), std::move(owner));
    auto* context = new Context{caller};
    const Status result = begin(context);
    if (result != 0) {
      delete context;
      Deliver(caller, result, nullptr, 0);
    }
    return result;
  }

  static void Complete(Context* context, Status status,
                       const uint8_t* bytes, size_t size) noexcept {
    Deliver(context->state, status, bytes, size);
  }
  static void Release(Context* context) noexcept { delete context; }

 private:
  static void Deliver(const std::shared_ptr<State>& state, Status status,
                      const uint8_t* bytes, size_t size) noexcept {
    if (state->completed.exchange(true)) return;
    Bytes owned;
    // Error/delete results carry no token. Copy borrowed success bytes before
    // the SDK returns from its callback and wipes its buffer.
    if (status == 0 && size) {
      if (!bytes) {
        status = static_cast<Status>(UINT32_C(0x80070057));  // E_INVALIDARG
      } else {
        try {
          owned.assign(bytes, bytes + size);
        } catch (...) {
          status = static_cast<Status>(UINT32_C(0x8007000e));  // E_OUTOFMEMORY
        }
      }
    }
    try {
      state->reply(status, std::move(owned));
    } catch (...) {
      // Never propagate a channel/dispatcher exception through the SDK C ABI.
    }
  }
};
}  // namespace flutter::winrt::plugins::firebase_messaging
