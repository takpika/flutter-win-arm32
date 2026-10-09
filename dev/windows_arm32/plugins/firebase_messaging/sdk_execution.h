// Copyright 2026 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <firebase_compat/sdk.h>

#include <atomic>
#include <functional>
#include <memory>

#include "sdk_message_handler.h"
namespace flutter::winrt::plugins::firebase_messaging {
// Callback custody only. SDK foreground replay and real OS task activation use
// the same lifetime guard; scheduling, visibility, deferral and cancellation
// belong to the SDK. A handler is constructed on its worker, never on OS UI.
class SdkExecution {
 public:
  using Completed = std::function<void(HRESULT, fcm_sdk_drain_result)>;
  using HandlerFactory = std::function<std::shared_ptr<SdkMessageHandler>()>;
  static HRESULT Drain(fcm_sdk_handle client, uint32_t limit,
                       std::shared_ptr<SdkMessageHandler> handler,
                       std::shared_ptr<std::atomic<bool>> closing,
                       std::shared_ptr<void> owner, Completed complete,
                       fcm_sdk_operation* operation) {
    return Start(
        [=](const fcm_sdk_background_callbacks* callbacks) noexcept {
          return fcm_sdk_start_drain(client, callbacks, limit, operation);
        },
        [handler = std::move(handler)] { return handler; }, std::move(closing),
        std::move(owner), std::move(complete));
  }
  static HRESULT BackgroundSaved(const char* app_name, void* real_task_instance,
                                 int kind, uint32_t limit,
                                 HandlerFactory factory,
                                 std::shared_ptr<void> owner,
                                 Completed complete) {
    // Call directly in the real BackgroundActivated callback: the SDK captures
    // the task's actual OS deferral before returning. No host worker/deferral.
    return Start(
        [=](const fcm_sdk_background_callbacks* callbacks) noexcept {
          return fcm_sdk_start_background_saved(app_name, real_task_instance,
                                                kind, callbacks, limit);
        },
        std::move(factory), {}, std::move(owner), std::move(complete));
  }

 private:
  struct State {
    HandlerFactory factory;
    std::shared_ptr<SdkMessageHandler> handler;
    std::shared_ptr<std::atomic<bool>> closing;
    std::shared_ptr<void> owner;
    Completed complete;
    fcm_sdk_background_cancellation cancellation{};
    std::atomic<bool> finished{false};
    State(HandlerFactory factory, std::shared_ptr<std::atomic<bool>> closing,
          std::shared_ptr<void> owner, Completed complete)
        : factory(std::move(factory)),
          closing(std::move(closing)),
          owner(std::move(owner)),
          complete(std::move(complete)) {}
    bool Closing() const { return closing && closing->load(); }
    void Finish(HRESULT result, fcm_sdk_drain_result drain) noexcept {
      if (finished.exchange(true)) return;
      try {
        if (complete) complete(result, drain);
      } catch (...) {
      }
    }
    HRESULT Dispatch(fcm_sdk_handle borrowed, int kind, int64_t receipt,
                     const uint8_t* json, size_t size) noexcept {
      try {
        if (!handler && factory) handler = factory();
        if (!handler) return E_INVALIDARG;
        return handler->Dispatch(
            kind, receipt, json, size,
            [this] {
              return Closing() ||
                     (cancellation.is_canceled &&
                      cancellation.is_canceled(cancellation.token));
            },
            borrowed);
      } catch (...) {
        return E_FAIL;
      }
    }
  };
  struct Context {
    std::shared_ptr<State> state;
  };
  template <class Begin>
  static HRESULT Start(Begin begin, HandlerFactory factory,
                       std::shared_ptr<std::atomic<bool>> closing,
                       std::shared_ptr<void> owner, Completed complete) {
    auto caller =
        std::make_shared<State>(std::move(factory), std::move(closing),
                                std::move(owner), std::move(complete));
    auto* context = new Context{caller};
    fcm_sdk_background_callbacks callbacks{};
    callbacks.size = sizeof(callbacks);
    callbacks.callbacks.size = sizeof(callbacks.callbacks);
    callbacks.callbacks.context = context;
    callbacks.callbacks.message =
        [](void* raw, fcm_sdk_handle borrowed, int kind, int64_t receipt,
           const uint8_t* json, size_t size) noexcept -> HRESULT {
      return static_cast<Context*>(raw)->state->Dispatch(borrowed, kind,
                                                         receipt, json, size);
    };
    callbacks.callbacks.canceled = [](void* raw) noexcept -> int {
      // Never query the SDK token from its composed host hook (recursion).
      return static_cast<Context*>(raw)->state->Closing() ? 1 : 0;
    };
    callbacks.started =
        [](void* raw, const fcm_sdk_background_cancellation* token) noexcept {
          if (token) static_cast<Context*>(raw)->state->cancellation = *token;
        };
    callbacks.completed = [](void* raw, HRESULT result,
                             const fcm_sdk_drain_result* drain) noexcept {
      static_cast<Context*>(raw)->state->Finish(
          result, drain ? *drain : fcm_sdk_drain_result{});
    };
    callbacks.release = [](void* raw) noexcept {
      delete static_cast<Context*>(raw);
    };
    const HRESULT result = begin(&callbacks);
    if (result != S_OK) {
      delete context;
      caller->Finish(result, {});
    }
    // Keep code/owner alive through API return even if release already ran.
    return result;
  }
};
}  // namespace flutter::winrt::plugins::firebase_messaging
